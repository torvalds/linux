/*
 * drivers/base/memory.c - basic Memory class support
 *
 * Written by Matt Tolentino <matthew.e.tolentino@intel.com>
 *            Dave Hansen <haveblue@us.ibm.com>
 *
 * This file provides the necessary infrastructure to represent
 * a SPARSEMEM-memory-model system's physical memory in /sysfs.
 * All arch-independent code that assumes MEMORY_HOTPLUG requires
 * SPARSEMEM should be contained here, or in mm/memory_hotplug.c.
 */

#include <linux/sysdev.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/topology.h>
#include <linux/capability.h>
#include <linux/device.h>
#include <linux/memory.h>
#include <linux/kobject.h>
#include <linux/memory_hotplug.h>
#include <linux/mm.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>

#define MEMORY_CLASS_NAME	"memory"

static struct sysdev_class memory_sysdev_class = {
	.name = MEMORY_CLASS_NAME,
};

static const char *memory_uevent_name(struct kset *kset, struct kobject *kobj)
{
	return MEMORY_CLASS_NAME;
}

static int memory_uevent(struct kset *kset, struct kobject *obj, struct kobj_uevent_env *env)
{
	int retval = 0;

	return retval;
}

static struct kset_uevent_ops memory_uevent_ops = {
	.name		= memory_uevent_name,
	.uevent		= memory_uevent,
};

static BLOCKING_NOTIFIER_HEAD(memory_chain);

int register_memory_notifier(struct notifier_block *nb)
{
        return blocking_notifier_chain_register(&memory_chain, nb);
}

void unregister_memory_notifier(struct notifier_block *nb)
{
        blocking_notifier_chain_unregister(&memory_chain, nb);
}

/*
 * register_memory - Setup a sysfs device for a memory block
 */
int register_memory(struct memory_block *memory, struct mem_section *section,
		struct node *root)
{
	int error;

	memory->sysdev.cls = &memory_sysdev_class;
	memory->sysdev.id = __section_nr(section);

	error = sysdev_register(&memory->sysdev);

	if (root && !error)
		error = sysfs_create_link(&root->sysdev.kobj,
					  &memory->sysdev.kobj,
					  kobject_name(&memory->sysdev.kobj));

	return error;
}

static void
unregister_memory(struct memory_block *memory, struct mem_section *section,
		struct node *root)
{
	BUG_ON(memory->sysdev.cls != &memory_sysdev_class);
	BUG_ON(memory->sysdev.id != __section_nr(section));

	sysdev_unregister(&memory->sysdev);
	if (root)
		sysfs_remove_link(&root->sysdev.kobj,
				  kobject_name(&memory->sysdev.kobj));
}

/*
 * use this as the physical section index that this memsection
 * uses.
 */

static ssize_t show_mem_phys_index(struct sys_device *dev, char *buf)
{
	struct memory_block *mem =
		container_of(dev, struct memory_block, sysdev);
	return sprintf(buf, "%08lx\n", mem->phys_index);
}

/*
 * online, offline, going offline, etc.
 */
static ssize_t show_mem_state(struct sys_device *dev, char *buf)
{
	struct memory_block *mem =
		container_of(dev, struct memory_block, sysdev);
	ssize_t len = 0;

	/*
	 * We can probably put these states in a nice little array
	 * so that they're not open-coded
	 */
	switch (mem->state) {
		case MEM_ONLINE:
			len = sprintf(buf, "online\n");
			break;
		case MEM_OFFLINE:
			len = sprintf(buf, "offline\n");
			break;
		case MEM_GOING_OFFLINE:
			len = sprintf(buf, "going-offline\n");
			break;
		default:
			len = sprintf(buf, "ERROR-UNKNOWN-%ld\n",
					mem->state);
			WARN_ON(1);
			break;
	}

	return len;
}

int memory_notify(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&memory_chain, val, v);
}

/*
 * MEMORY_HOTPLUG depends on SPARSEMEM in mm/Kconfig, so it is
 * OK to have direct references to sparsemem variables in here.
 */
static int
memory_block_action(struct memory_block *mem, unsigned long action)
{
	int i;
	unsigned long psection;
	unsigned long start_pfn, start_paddr;
	struct page *first_page;
	int ret;
	int old_state = mem->state;

	psection = mem->phys_index;
	first_page = pfn_to_page(psection << PFN_SECTION_SHIFT);

	/*
	 * The probe routines leave the pages reserved, just
	 * as the bootmem code does.  Make sure they're still
	 * that way.
	 */
	if (action == MEM_ONLINE) {
		for (i = 0; i < PAGES_PER_SECTION; i++) {
			if (PageReserved(first_page+i))
				continue;

			printk(KERN_WARNING "section number %ld page number %d "
				"not reserved, was it already online? \n",
				psection, i);
			return -EBUSY;
		}
	}

	switch (action) {
		case MEM_ONLINE:
			start_pfn = page_to_pfn(first_page);
			ret = online_pages(start_pfn, PAGES_PER_SECTION);
			break;
		case MEM_OFFLINE:
			mem->state = MEM_GOING_OFFLINE;
			start_paddr = page_to_pfn(first_page) << PAGE_SHIFT;
			ret = remove_memory(start_paddr,
					    PAGES_PER_SECTION << PAGE_SHIFT);
			if (ret) {
				mem->state = old_state;
				break;
			}
			break;
		default:
			printk(KERN_WARNING "%s(%p, %ld) unknown action: %ld\n",
					__FUNCTION__, mem, action, action);
			WARN_ON(1);
			ret = -EINVAL;
	}

	return ret;
}

static int memory_block_change_state(struct memory_block *mem,
		unsigned long to_state, unsigned long from_state_req)
{
	int ret = 0;
	down(&mem->state_sem);

	if (mem->state != from_state_req) {
		ret = -EINVAL;
		goto out;
	}

	ret = memory_block_action(mem, to_state);
	if (!ret)
		mem->state = to_state;

out:
	up(&mem->state_sem);
	return ret;
}

static ssize_t
store_mem_state(struct sys_device *dev, const char *buf, size_t count)
{
	struct memory_block *mem;
	unsigned int phys_section_nr;
	int ret = -EINVAL;

	mem = container_of(dev, struct memory_block, sysdev);
	phys_section_nr = mem->phys_index;

	if (!present_section_nr(phys_section_nr))
		goto out;

	if (!strncmp(buf, "online", min((int)count, 6)))
		ret = memory_block_change_state(mem, MEM_ONLINE, MEM_OFFLINE);
	else if(!strncmp(buf, "offline", min((int)count, 7)))
		ret = memory_block_change_state(mem, MEM_OFFLINE, MEM_ONLINE);
out:
	if (ret)
		return ret;
	return count;
}

/*
 * phys_device is a bad name for this.  What I really want
 * is a way to differentiate between memory ranges that
 * are part of physical devices that constitute
 * a complete removable unit or fru.
 * i.e. do these ranges belong to the same physical device,
 * s.t. if I offline all of these sections I can then
 * remove the physical device?
 */
static ssize_t show_phys_device(struct sys_device *dev, char *buf)
{
	struct memory_block *mem =
		container_of(dev, struct memory_block, sysdev);
	return sprintf(buf, "%d\n", mem->phys_device);
}

static SYSDEV_ATTR(phys_index, 0444, show_mem_phys_index, NULL);
static SYSDEV_ATTR(state, 0644, show_mem_state, store_mem_state);
static SYSDEV_ATTR(phys_device, 0444, show_phys_device, NULL);

#define mem_create_simple_file(mem, attr_name)	\
	sysdev_create_file(&mem->sysdev, &attr_##attr_name)
#define mem_remove_simple_file(mem, attr_name)	\
	sysdev_remove_file(&mem->sysdev, &attr_##attr_name)

/*
 * Block size attribute stuff
 */
static ssize_t
print_block_size(struct class *class, char *buf)
{
	return sprintf(buf, "%lx\n", (unsigned long)PAGES_PER_SECTION * PAGE_SIZE);
}

static CLASS_ATTR(block_size_bytes, 0444, print_block_size, NULL);

static int block_size_init(void)
{
	return sysfs_create_file(&memory_sysdev_class.kset.kobj,
				&class_attr_block_size_bytes.attr);
}

/*
 * Some architectures will have custom drivers to do this, and
 * will not need to do it from userspace.  The fake hot-add code
 * as well as ppc64 will do all of their discovery in userspace
 * and will require this interface.
 */
#ifdef CONFIG_ARCH_MEMORY_PROBE
static ssize_t
memory_probe_store(struct class *class, const char *buf, size_t count)
{
	u64 phys_addr;
	int nid;
	int ret;

	phys_addr = simple_strtoull(buf, NULL, 0);

	nid = memory_add_physaddr_to_nid(phys_addr);
	ret = add_memory(nid, phys_addr, PAGES_PER_SECTION << PAGE_SHIFT);

	if (ret)
		count = ret;

	return count;
}
static CLASS_ATTR(probe, 0700, NULL, memory_probe_store);

static int memory_probe_init(void)
{
	return sysfs_create_file(&memory_sysdev_class.kset.kobj,
				&class_attr_probe.attr);
}
#else
static inline int memory_probe_init(void)
{
	return 0;
}
#endif

/*
 * Note that phys_device is optional.  It is here to allow for
 * differentiation between which *physical* devices each
 * section belongs to...
 */

static int add_memory_block(unsigned long node_id, struct mem_section *section,
		     unsigned long state, int phys_device)
{
	struct memory_block *mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	int ret = 0;

	if (!mem)
		return -ENOMEM;

	mem->phys_index = __section_nr(section);
	mem->state = state;
	init_MUTEX(&mem->state_sem);
	mem->phys_device = phys_device;

	ret = register_memory(mem, section, NULL);
	if (!ret)
		ret = mem_create_simple_file(mem, phys_index);
	if (!ret)
		ret = mem_create_simple_file(mem, state);
	if (!ret)
		ret = mem_create_simple_file(mem, phys_device);

	return ret;
}

/*
 * For now, we have a linear search to go find the appropriate
 * memory_block corresponding to a particular phys_index. If
 * this gets to be a real problem, we can always use a radix
 * tree or something here.
 *
 * This could be made generic for all sysdev classes.
 */
static struct memory_block *find_memory_block(struct mem_section *section)
{
	struct kobject *kobj;
	struct sys_device *sysdev;
	struct memory_block *mem;
	char name[sizeof(MEMORY_CLASS_NAME) + 9 + 1];

	/*
	 * This only works because we know that section == sysdev->id
	 * slightly redundant with sysdev_register()
	 */
	sprintf(&name[0], "%s%d", MEMORY_CLASS_NAME, __section_nr(section));

	kobj = kset_find_obj(&memory_sysdev_class.kset, name);
	if (!kobj)
		return NULL;

	sysdev = container_of(kobj, struct sys_device, kobj);
	mem = container_of(sysdev, struct memory_block, sysdev);

	return mem;
}

int remove_memory_block(unsigned long node_id, struct mem_section *section,
		int phys_device)
{
	struct memory_block *mem;

	mem = find_memory_block(section);
	mem_remove_simple_file(mem, phys_index);
	mem_remove_simple_file(mem, state);
	mem_remove_simple_file(mem, phys_device);
	unregister_memory(mem, section, NULL);

	return 0;
}

/*
 * need an interface for the VM to add new memory regions,
 * but without onlining it.
 */
int register_new_memory(struct mem_section *section)
{
	return add_memory_block(0, section, MEM_OFFLINE, 0);
}

int unregister_memory_section(struct mem_section *section)
{
	if (!present_section(section))
		return -EINVAL;

	return remove_memory_block(0, section, 0);
}

/*
 * Initialize the sysfs support for memory devices...
 */
int __init memory_dev_init(void)
{
	unsigned int i;
	int ret;
	int err;

	memory_sysdev_class.kset.uevent_ops = &memory_uevent_ops;
	ret = sysdev_class_register(&memory_sysdev_class);
	if (ret)
		goto out;

	/*
	 * Create entries for memory sections that were found
	 * during boot and have been initialized
	 */
	for (i = 0; i < NR_MEM_SECTIONS; i++) {
		if (!present_section_nr(i))
			continue;
		err = add_memory_block(0, __nr_to_section(i), MEM_ONLINE, 0);
		if (!ret)
			ret = err;
	}

	err = memory_probe_init();
	if (!ret)
		ret = err;
	err = block_size_init();
	if (!ret)
		ret = err;
out:
	if (ret)
		printk(KERN_ERR "%s() failed: %d\n", __FUNCTION__, ret);
	return ret;
}

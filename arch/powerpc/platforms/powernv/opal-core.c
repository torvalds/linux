// SPDX-License-Identifier: GPL-2.0-only
/*
 * Interface for exporting the OPAL ELF core.
 * Heavily inspired from fs/proc/vmcore.c
 *
 * Copyright 2019, Hari Bathini, IBM Corporation.
 */

#define pr_fmt(fmt) "opal core: " fmt

#include <linux/memblock.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/elf.h>
#include <linux/elfcore.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/vmcore_info.h>
#include <linux/of.h>

#include <asm/page.h>
#include <asm/opal.h>
#include <asm/fadump-internal.h>

#include "opal-fadump.h"

#define MAX_PT_LOAD_CNT		8

/* NT_AUXV note related info */
#define AUXV_CNT		1
#define AUXV_DESC_SZ		(((2 * AUXV_CNT) + 1) * sizeof(Elf64_Off))

struct opalcore_config {
	u32			num_cpus;
	/* PIR value of crashing CPU */
	u32			crashing_cpu;

	/* CPU state data info from F/W */
	u64			cpu_state_destination_vaddr;
	u64			cpu_state_data_size;
	u64			cpu_state_entry_size;

	/* OPAL memory to be exported as PT_LOAD segments */
	u64			ptload_addr[MAX_PT_LOAD_CNT];
	u64			ptload_size[MAX_PT_LOAD_CNT];
	u64			ptload_cnt;

	/* Pointer to the first PT_LOAD in the ELF core file */
	Elf64_Phdr		*ptload_phdr;

	/* Total size of opalcore file. */
	size_t			opalcore_size;

	/* Buffer for all the ELF core headers and the PT_NOTE */
	size_t			opalcorebuf_sz;
	char			*opalcorebuf;

	/* NT_AUXV buffer */
	char			auxv_buf[AUXV_DESC_SZ];
};

struct opalcore {
	struct list_head	list;
	u64			paddr;
	size_t			size;
	loff_t			offset;
};

static LIST_HEAD(opalcore_list);
static struct opalcore_config *oc_conf;
static const struct opal_mpipl_fadump *opalc_metadata;
static const struct opal_mpipl_fadump *opalc_cpu_metadata;
static struct kobject *mpipl_kobj;

/*
 * Set crashing CPU's signal to SIGUSR1. if the kernel is triggered
 * by kernel, SIGTERM otherwise.
 */
bool kernel_initiated;

static struct opalcore * __init get_new_element(void)
{
	return kzalloc(sizeof(struct opalcore), GFP_KERNEL);
}

static inline int is_opalcore_usable(void)
{
	return (oc_conf && oc_conf->opalcorebuf != NULL) ? 1 : 0;
}

static Elf64_Word *__init append_elf64_note(Elf64_Word *buf, char *name,
				     u32 type, void *data,
				     size_t data_len)
{
	Elf64_Nhdr *note = (Elf64_Nhdr *)buf;
	Elf64_Word namesz = strlen(name) + 1;

	note->n_namesz = cpu_to_be32(namesz);
	note->n_descsz = cpu_to_be32(data_len);
	note->n_type   = cpu_to_be32(type);
	buf += DIV_ROUND_UP(sizeof(*note), sizeof(Elf64_Word));
	memcpy(buf, name, namesz);
	buf += DIV_ROUND_UP(namesz, sizeof(Elf64_Word));
	memcpy(buf, data, data_len);
	buf += DIV_ROUND_UP(data_len, sizeof(Elf64_Word));

	return buf;
}

static void __init fill_prstatus(struct elf_prstatus *prstatus, int pir,
			  struct pt_regs *regs)
{
	memset(prstatus, 0, sizeof(struct elf_prstatus));
	elf_core_copy_regs(&(prstatus->pr_reg), regs);

	/*
	 * Overload PID with PIR value.
	 * As a PIR value could also be '0', add an offset of '100'
	 * to every PIR to avoid misinterpretations in GDB.
	 */
	prstatus->common.pr_pid  = cpu_to_be32(100 + pir);
	prstatus->common.pr_ppid = cpu_to_be32(1);

	/*
	 * Indicate SIGUSR1 for crash initiated from kernel.
	 * SIGTERM otherwise.
	 */
	if (pir == oc_conf->crashing_cpu) {
		short sig;

		sig = kernel_initiated ? SIGUSR1 : SIGTERM;
		prstatus->common.pr_cursig = cpu_to_be16(sig);
	}
}

static Elf64_Word *__init auxv_to_elf64_notes(Elf64_Word *buf,
				       u64 opal_boot_entry)
{
	Elf64_Off *bufp = (Elf64_Off *)oc_conf->auxv_buf;
	int idx = 0;

	memset(bufp, 0, AUXV_DESC_SZ);

	/* Entry point of OPAL */
	bufp[idx++] = cpu_to_be64(AT_ENTRY);
	bufp[idx++] = cpu_to_be64(opal_boot_entry);

	/* end of vector */
	bufp[idx++] = cpu_to_be64(AT_NULL);

	buf = append_elf64_note(buf, NN_AUXV, NT_AUXV,
				oc_conf->auxv_buf, AUXV_DESC_SZ);
	return buf;
}

/*
 * Read from the ELF header and then the crash dump.
 * Returns number of bytes read on success, -errno on failure.
 */
static ssize_t read_opalcore(struct file *file, struct kobject *kobj,
			     const struct bin_attribute *bin_attr, char *to,
			     loff_t pos, size_t count)
{
	struct opalcore *m;
	ssize_t tsz, avail;
	loff_t tpos = pos;

	if (pos >= oc_conf->opalcore_size)
		return 0;

	/* Adjust count if it goes beyond opalcore size */
	avail = oc_conf->opalcore_size - pos;
	if (count > avail)
		count = avail;

	if (count == 0)
		return 0;

	/* Read ELF core header and/or PT_NOTE segment */
	if (tpos < oc_conf->opalcorebuf_sz) {
		tsz = min_t(size_t, oc_conf->opalcorebuf_sz - tpos, count);
		memcpy(to, oc_conf->opalcorebuf + tpos, tsz);
		to += tsz;
		tpos += tsz;
		count -= tsz;
	}

	list_for_each_entry(m, &opalcore_list, list) {
		/* nothing more to read here */
		if (count == 0)
			break;

		if (tpos < m->offset + m->size) {
			void *addr;

			tsz = min_t(size_t, m->offset + m->size - tpos, count);
			addr = (void *)(m->paddr + tpos - m->offset);
			memcpy(to, __va(addr), tsz);
			to += tsz;
			tpos += tsz;
			count -= tsz;
		}
	}

	return (tpos - pos);
}

static struct bin_attribute opal_core_attr __ro_after_init = {
	.attr = {.name = "core", .mode = 0400},
	.read_new = read_opalcore
};

/*
 * Read CPU state dump data and convert it into ELF notes.
 *
 * Each register entry is of 16 bytes, A numerical identifier along with
 * a GPR/SPR flag in the first 8 bytes and the register value in the next
 * 8 bytes. For more details refer to F/W documentation.
 */
static Elf64_Word * __init opalcore_append_cpu_notes(Elf64_Word *buf)
{
	u32 thread_pir, size_per_thread, regs_offset, regs_cnt, reg_esize;
	struct hdat_fadump_thread_hdr *thdr;
	struct elf_prstatus prstatus;
	Elf64_Word *first_cpu_note;
	struct pt_regs regs;
	char *bufp;
	int i;

	size_per_thread = oc_conf->cpu_state_entry_size;
	bufp = __va(oc_conf->cpu_state_destination_vaddr);

	/*
	 * Offset for register entries, entry size and registers count is
	 * duplicated in every thread header in keeping with HDAT format.
	 * Use these values from the first thread header.
	 */
	thdr = (struct hdat_fadump_thread_hdr *)bufp;
	regs_offset = (offsetof(struct hdat_fadump_thread_hdr, offset) +
		       be32_to_cpu(thdr->offset));
	reg_esize = be32_to_cpu(thdr->esize);
	regs_cnt  = be32_to_cpu(thdr->ecnt);

	pr_debug("--------CPU State Data------------\n");
	pr_debug("NumCpus     : %u\n", oc_conf->num_cpus);
	pr_debug("\tOffset: %u, Entry size: %u, Cnt: %u\n",
		 regs_offset, reg_esize, regs_cnt);

	/*
	 * Skip past the first CPU note. Fill this note with the
	 * crashing CPU's prstatus.
	 */
	first_cpu_note = buf;
	buf = append_elf64_note(buf, NN_PRSTATUS, NT_PRSTATUS,
				&prstatus, sizeof(prstatus));

	for (i = 0; i < oc_conf->num_cpus; i++, bufp += size_per_thread) {
		thdr = (struct hdat_fadump_thread_hdr *)bufp;
		thread_pir = be32_to_cpu(thdr->pir);

		pr_debug("[%04d] PIR: 0x%x, core state: 0x%02x\n",
			 i, thread_pir, thdr->core_state);

		/*
		 * Register state data of MAX cores is provided by firmware,
		 * but some of this cores may not be active. So, while
		 * processing register state data, check core state and
		 * skip threads that belong to inactive cores.
		 */
		if (thdr->core_state == HDAT_FADUMP_CORE_INACTIVE)
			continue;

		opal_fadump_read_regs((bufp + regs_offset), regs_cnt,
				      reg_esize, false, &regs);

		pr_debug("PIR 0x%x - R1 : 0x%llx, NIP : 0x%llx\n", thread_pir,
			 be64_to_cpu(regs.gpr[1]), be64_to_cpu(regs.nip));
		fill_prstatus(&prstatus, thread_pir, &regs);

		if (thread_pir != oc_conf->crashing_cpu) {
			buf = append_elf64_note(buf, NN_PRSTATUS,
						NT_PRSTATUS, &prstatus,
						sizeof(prstatus));
		} else {
			/*
			 * Add crashing CPU as the first NT_PRSTATUS note for
			 * GDB to process the core file appropriately.
			 */
			append_elf64_note(first_cpu_note, NN_PRSTATUS,
					  NT_PRSTATUS, &prstatus,
					  sizeof(prstatus));
		}
	}

	return buf;
}

static int __init create_opalcore(void)
{
	u64 opal_boot_entry, opal_base_addr, paddr;
	u32 hdr_size, cpu_notes_size, count;
	struct device_node *dn;
	struct opalcore *new;
	loff_t opalcore_off;
	struct page *page;
	Elf64_Phdr *phdr;
	Elf64_Ehdr *elf;
	int i, ret;
	char *bufp;

	/* Get size of header & CPU notes for OPAL core */
	hdr_size = (sizeof(Elf64_Ehdr) +
		    ((oc_conf->ptload_cnt + 1) * sizeof(Elf64_Phdr)));
	cpu_notes_size = ((oc_conf->num_cpus * (CRASH_CORE_NOTE_HEAD_BYTES +
			  CRASH_CORE_NOTE_NAME_BYTES +
			  CRASH_CORE_NOTE_DESC_BYTES)) +
			  (CRASH_CORE_NOTE_HEAD_BYTES +
			  CRASH_CORE_NOTE_NAME_BYTES + AUXV_DESC_SZ));

	/* Allocate buffer to setup OPAL core */
	oc_conf->opalcorebuf_sz = PAGE_ALIGN(hdr_size + cpu_notes_size);
	oc_conf->opalcorebuf = alloc_pages_exact(oc_conf->opalcorebuf_sz,
						 GFP_KERNEL | __GFP_ZERO);
	if (!oc_conf->opalcorebuf) {
		pr_err("Not enough memory to setup OPAL core (size: %lu)\n",
		       oc_conf->opalcorebuf_sz);
		oc_conf->opalcorebuf_sz = 0;
		return -ENOMEM;
	}
	count = oc_conf->opalcorebuf_sz / PAGE_SIZE;
	page = virt_to_page(oc_conf->opalcorebuf);
	for (i = 0; i < count; i++)
		mark_page_reserved(page + i);

	pr_debug("opalcorebuf = 0x%llx\n", (u64)oc_conf->opalcorebuf);

	/* Read OPAL related device-tree entries */
	dn = of_find_node_by_name(NULL, "ibm,opal");
	if (dn) {
		ret = of_property_read_u64(dn, "opal-base-address",
					   &opal_base_addr);
		pr_debug("opal-base-address: %llx\n", opal_base_addr);
		ret |= of_property_read_u64(dn, "opal-boot-address",
					    &opal_boot_entry);
		pr_debug("opal-boot-address: %llx\n", opal_boot_entry);
	}
	if (!dn || ret)
		pr_warn("WARNING: Failed to read OPAL base & entry values\n");

	of_node_put(dn);

	/* Use count to keep track of the program headers */
	count = 0;

	bufp = oc_conf->opalcorebuf;
	elf = (Elf64_Ehdr *)bufp;
	bufp += sizeof(Elf64_Ehdr);
	memcpy(elf->e_ident, ELFMAG, SELFMAG);
	elf->e_ident[EI_CLASS] = ELF_CLASS;
	elf->e_ident[EI_DATA] = ELFDATA2MSB;
	elf->e_ident[EI_VERSION] = EV_CURRENT;
	elf->e_ident[EI_OSABI] = ELF_OSABI;
	memset(elf->e_ident+EI_PAD, 0, EI_NIDENT-EI_PAD);
	elf->e_type = cpu_to_be16(ET_CORE);
	elf->e_machine = cpu_to_be16(ELF_ARCH);
	elf->e_version = cpu_to_be32(EV_CURRENT);
	elf->e_entry = 0;
	elf->e_phoff = cpu_to_be64(sizeof(Elf64_Ehdr));
	elf->e_shoff = 0;
	elf->e_flags = 0;

	elf->e_ehsize = cpu_to_be16(sizeof(Elf64_Ehdr));
	elf->e_phentsize = cpu_to_be16(sizeof(Elf64_Phdr));
	elf->e_phnum = 0;
	elf->e_shentsize = 0;
	elf->e_shnum = 0;
	elf->e_shstrndx = 0;

	phdr = (Elf64_Phdr *)bufp;
	bufp += sizeof(Elf64_Phdr);
	phdr->p_type	= cpu_to_be32(PT_NOTE);
	phdr->p_flags	= 0;
	phdr->p_align	= 0;
	phdr->p_paddr	= phdr->p_vaddr = 0;
	phdr->p_offset	= cpu_to_be64(hdr_size);
	phdr->p_filesz	= phdr->p_memsz = cpu_to_be64(cpu_notes_size);
	count++;

	opalcore_off = oc_conf->opalcorebuf_sz;
	oc_conf->ptload_phdr  = (Elf64_Phdr *)bufp;
	paddr = 0;
	for (i = 0; i < oc_conf->ptload_cnt; i++) {
		phdr = (Elf64_Phdr *)bufp;
		bufp += sizeof(Elf64_Phdr);
		phdr->p_type	= cpu_to_be32(PT_LOAD);
		phdr->p_flags	= cpu_to_be32(PF_R|PF_W|PF_X);
		phdr->p_align	= 0;

		new = get_new_element();
		if (!new)
			return -ENOMEM;
		new->paddr  = oc_conf->ptload_addr[i];
		new->size   = oc_conf->ptload_size[i];
		new->offset = opalcore_off;
		list_add_tail(&new->list, &opalcore_list);

		phdr->p_paddr	= cpu_to_be64(paddr);
		phdr->p_vaddr	= cpu_to_be64(opal_base_addr + paddr);
		phdr->p_filesz	= phdr->p_memsz  =
			cpu_to_be64(oc_conf->ptload_size[i]);
		phdr->p_offset	= cpu_to_be64(opalcore_off);

		count++;
		opalcore_off += oc_conf->ptload_size[i];
		paddr += oc_conf->ptload_size[i];
	}

	elf->e_phnum = cpu_to_be16(count);

	bufp = (char *)opalcore_append_cpu_notes((Elf64_Word *)bufp);
	bufp = (char *)auxv_to_elf64_notes((Elf64_Word *)bufp, opal_boot_entry);

	oc_conf->opalcore_size = opalcore_off;
	return 0;
}

static void opalcore_cleanup(void)
{
	if (oc_conf == NULL)
		return;

	/* Remove OPAL core sysfs file */
	sysfs_remove_bin_file(mpipl_kobj, &opal_core_attr);
	oc_conf->ptload_phdr = NULL;
	oc_conf->ptload_cnt = 0;

	/* free the buffer used for setting up OPAL core */
	if (oc_conf->opalcorebuf) {
		void *end = (void *)((u64)oc_conf->opalcorebuf +
				     oc_conf->opalcorebuf_sz);

		free_reserved_area(oc_conf->opalcorebuf, end, -1, NULL);
		oc_conf->opalcorebuf = NULL;
		oc_conf->opalcorebuf_sz = 0;
	}

	kfree(oc_conf);
	oc_conf = NULL;
}
__exitcall(opalcore_cleanup);

static void __init opalcore_config_init(void)
{
	u32 idx, cpu_data_version;
	struct device_node *np;
	const __be32 *prop;
	u64 addr = 0;
	int i, ret;

	np = of_find_node_by_path("/ibm,opal/dump");
	if (np == NULL)
		return;

	if (!of_device_is_compatible(np, "ibm,opal-dump")) {
		pr_warn("Support missing for this f/w version!\n");
		return;
	}

	/* Check if dump has been initiated on last reboot */
	prop = of_get_property(np, "mpipl-boot", NULL);
	if (!prop) {
		of_node_put(np);
		return;
	}

	/* Get OPAL metadata */
	ret = opal_mpipl_query_tag(OPAL_MPIPL_TAG_OPAL, &addr);
	if ((ret != OPAL_SUCCESS) || !addr) {
		pr_err("Failed to get OPAL metadata (%d)\n", ret);
		goto error_out;
	}

	addr = be64_to_cpu(addr);
	pr_debug("OPAL metadata addr: %llx\n", addr);
	opalc_metadata = __va(addr);

	/* Get OPAL CPU metadata */
	ret = opal_mpipl_query_tag(OPAL_MPIPL_TAG_CPU, &addr);
	if ((ret != OPAL_SUCCESS) || !addr) {
		pr_err("Failed to get OPAL CPU metadata (%d)\n", ret);
		goto error_out;
	}

	addr = be64_to_cpu(addr);
	pr_debug("CPU metadata addr: %llx\n", addr);
	opalc_cpu_metadata = __va(addr);

	/* Allocate memory for config buffer */
	oc_conf = kzalloc(sizeof(struct opalcore_config), GFP_KERNEL);
	if (oc_conf == NULL)
		goto error_out;

	/* Parse OPAL metadata */
	if (opalc_metadata->version != OPAL_MPIPL_VERSION) {
		pr_warn("Supported OPAL metadata version: %u, found: %u!\n",
			OPAL_MPIPL_VERSION, opalc_metadata->version);
		pr_warn("WARNING: F/W using newer OPAL metadata format!!\n");
	}

	oc_conf->ptload_cnt = 0;
	idx = be32_to_cpu(opalc_metadata->region_cnt);
	if (idx > MAX_PT_LOAD_CNT) {
		pr_warn("WARNING: OPAL regions count (%d) adjusted to limit (%d)",
			idx, MAX_PT_LOAD_CNT);
		idx = MAX_PT_LOAD_CNT;
	}
	for (i = 0; i < idx; i++) {
		oc_conf->ptload_addr[oc_conf->ptload_cnt] =
				be64_to_cpu(opalc_metadata->region[i].dest);
		oc_conf->ptload_size[oc_conf->ptload_cnt++] =
				be64_to_cpu(opalc_metadata->region[i].size);
	}
	oc_conf->ptload_cnt = i;
	oc_conf->crashing_cpu = be32_to_cpu(opalc_metadata->crashing_pir);

	if (!oc_conf->ptload_cnt) {
		pr_err("OPAL memory regions not found\n");
		goto error_out;
	}

	/* Parse OPAL CPU metadata */
	cpu_data_version = be32_to_cpu(opalc_cpu_metadata->cpu_data_version);
	if (cpu_data_version != HDAT_FADUMP_CPU_DATA_VER) {
		pr_warn("Supported CPU data version: %u, found: %u!\n",
			HDAT_FADUMP_CPU_DATA_VER, cpu_data_version);
		pr_warn("WARNING: F/W using newer CPU state data format!!\n");
	}

	addr = be64_to_cpu(opalc_cpu_metadata->region[0].dest);
	if (!addr) {
		pr_err("CPU state data not found!\n");
		goto error_out;
	}
	oc_conf->cpu_state_destination_vaddr = (u64)__va(addr);

	oc_conf->cpu_state_data_size =
			be64_to_cpu(opalc_cpu_metadata->region[0].size);
	oc_conf->cpu_state_entry_size =
			be32_to_cpu(opalc_cpu_metadata->cpu_data_size);

	if ((oc_conf->cpu_state_entry_size == 0) ||
	    (oc_conf->cpu_state_entry_size > oc_conf->cpu_state_data_size)) {
		pr_err("CPU state data is invalid.\n");
		goto error_out;
	}
	oc_conf->num_cpus = (oc_conf->cpu_state_data_size /
			     oc_conf->cpu_state_entry_size);

	of_node_put(np);
	return;

error_out:
	pr_err("Could not export /sys/firmware/opal/core\n");
	opalcore_cleanup();
	of_node_put(np);
}

static ssize_t release_core_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	int input = -1;

	if (kstrtoint(buf, 0, &input))
		return -EINVAL;

	if (input == 1) {
		if (oc_conf == NULL) {
			pr_err("'/sys/firmware/opal/core' file not accessible!\n");
			return -EPERM;
		}

		/*
		 * Take away '/sys/firmware/opal/core' and release all memory
		 * used for exporting this file.
		 */
		opalcore_cleanup();
	} else
		return -EINVAL;

	return count;
}

static struct kobj_attribute opalcore_rel_attr = __ATTR_WO(release_core);

static struct attribute *mpipl_attr[] = {
	&opalcore_rel_attr.attr,
	NULL,
};

static const struct bin_attribute *const mpipl_bin_attr[] = {
	&opal_core_attr,
	NULL,

};

static const struct attribute_group mpipl_group = {
	.attrs = mpipl_attr,
	.bin_attrs_new =  mpipl_bin_attr,
};

static int __init opalcore_init(void)
{
	int rc = -1;

	opalcore_config_init();

	if (oc_conf == NULL)
		return rc;

	create_opalcore();

	/*
	 * If oc_conf->opalcorebuf= is set in the 2nd kernel,
	 * then capture the dump.
	 */
	if (!(is_opalcore_usable())) {
		pr_err("Failed to export /sys/firmware/opal/mpipl/core\n");
		opalcore_cleanup();
		return rc;
	}

	/* Set OPAL core file size */
	opal_core_attr.size = oc_conf->opalcore_size;

	mpipl_kobj = kobject_create_and_add("mpipl", opal_kobj);
	if (!mpipl_kobj) {
		pr_err("unable to create mpipl kobject\n");
		return -ENOMEM;
	}

	/* Export OPAL core sysfs file */
	rc = sysfs_create_group(mpipl_kobj, &mpipl_group);
	if (rc) {
		pr_err("mpipl sysfs group creation failed (%d)", rc);
		opalcore_cleanup();
		return rc;
	}
	/* The /sys/firmware/opal/core is moved to /sys/firmware/opal/mpipl/
	 * directory, need to create symlink at old location to maintain
	 * backward compatibility.
	 */
	rc = compat_only_sysfs_link_entry_to_kobj(opal_kobj, mpipl_kobj,
						  "core", NULL);
	if (rc) {
		pr_err("unable to create core symlink (%d)\n", rc);
		return rc;
	}

	return 0;
}
fs_initcall(opalcore_init);

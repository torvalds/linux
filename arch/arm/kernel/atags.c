#include <linux/slab.h>
#include <linux/kexec.h>
#include <linux/proc_fs.h>
#include <asm/setup.h>
#include <asm/types.h>
#include <asm/page.h>

struct buffer {
	size_t size;
	char *data;
};
static struct buffer tags_buffer;

static int
read_buffer(char* page, char** start, off_t off, int count,
	int* eof, void* data)
{
	struct buffer *buffer = (struct buffer *)data;

	if (off >= buffer->size) {
		*eof = 1;
		return 0;
	}

	count = min((int) (buffer->size - off), count);

	memcpy(page, &buffer->data[off], count);

	return count;
}


static int
create_proc_entries(void)
{
	struct proc_dir_entry* tags_entry;

	tags_entry = create_proc_read_entry("atags", 0400, NULL, read_buffer, &tags_buffer);
	if (!tags_entry)
		return -ENOMEM;

	return 0;
}


static char __initdata atags_copy_buf[KEXEC_BOOT_PARAMS_SIZE];
static char __initdata *atags_copy;

void __init save_atags(const struct tag *tags)
{
	atags_copy = atags_copy_buf;
	memcpy(atags_copy, tags, KEXEC_BOOT_PARAMS_SIZE);
}


static int __init init_atags_procfs(void)
{
	struct tag *tag;
	int error;

	if (!atags_copy) {
		printk(KERN_WARNING "Exporting ATAGs: No saved tags found\n");
		return -EIO;
	}

	for (tag = (struct tag *) atags_copy; tag->hdr.size; tag = tag_next(tag))
		;

	tags_buffer.size = ((char *) tag - atags_copy) + sizeof(tag->hdr);
	tags_buffer.data = kmalloc(tags_buffer.size, GFP_KERNEL);
	if (tags_buffer.data == NULL)
		return -ENOMEM;
	memcpy(tags_buffer.data, atags_copy, tags_buffer.size);

	error = create_proc_entries();
	if (error) {
		printk(KERN_ERR "Exporting ATAGs: not enough memory\n");
		kfree(tags_buffer.data);
		tags_buffer.size = 0;
		tags_buffer.data = NULL;
	}

	return error;
}

arch_initcall(init_atags_procfs);

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/types.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>

#define SAMPLE_BUFFER_SIZE 8192

static char* sample_buffer;
static char* sample_buffer_pos;
static int prof_running = 0;

void
cris_profile_sample(struct pt_regs* regs)
{
	if (!prof_running)
		return;

	if (user_mode(regs))
		*(unsigned int*)sample_buffer_pos = current->pid;
	else
		*(unsigned int*)sample_buffer_pos = 0;

	*(unsigned int*)(sample_buffer_pos + 4) = instruction_pointer(regs);
	sample_buffer_pos += 8;

	if (sample_buffer_pos == sample_buffer + SAMPLE_BUFFER_SIZE)
		sample_buffer_pos = sample_buffer;
}

static ssize_t
read_cris_profile(struct file *file, char __user *buf,
		  size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;

	if (p > SAMPLE_BUFFER_SIZE)
		return 0;

	if (p + count > SAMPLE_BUFFER_SIZE)
		count = SAMPLE_BUFFER_SIZE - p;
	if (copy_to_user(buf, sample_buffer + p,count))
		return -EFAULT;

	memset(sample_buffer + p, 0, count);
	*ppos += count;

	return count;
}

static ssize_t
write_cris_profile(struct file *file, const char __user *buf,
		   size_t count, loff_t *ppos)
{
	sample_buffer_pos = sample_buffer;
	memset(sample_buffer, 0, SAMPLE_BUFFER_SIZE);
}

static const struct file_operations cris_proc_profile_operations = {
	.read		= read_cris_profile,
	.write		= write_cris_profile,
};

static int
__init init_cris_profile(void)
{
	struct proc_dir_entry *entry;

	sample_buffer = kmalloc(SAMPLE_BUFFER_SIZE, GFP_KERNEL);
	if (!sample_buffer) {
		return -ENOMEM;
	}

	sample_buffer_pos = sample_buffer;

	entry = proc_create("system_profile", S_IWUSR | S_IRUGO, NULL,
			    &cris_proc_profile_operations);
	if (entry) {
		entry->size = SAMPLE_BUFFER_SIZE;
	}
	prof_running = 1;

	return 0;
}

__initcall(init_cris_profile);

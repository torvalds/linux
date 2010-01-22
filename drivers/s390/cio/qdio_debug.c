/*
 *  drivers/s390/cio/qdio_debug.c
 *
 *  Copyright IBM Corp. 2008,2009
 *
 *  Author: Jan Glauber (jang@linux.vnet.ibm.com)
 */
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <asm/debug.h>
#include "qdio_debug.h"
#include "qdio.h"

debug_info_t *qdio_dbf_setup;
debug_info_t *qdio_dbf_error;

static struct dentry *debugfs_root;
#define QDIO_DEBUGFS_NAME_LEN	10

void qdio_allocate_dbf(struct qdio_initialize *init_data,
		       struct qdio_irq *irq_ptr)
{
	char text[20];

	DBF_EVENT("qfmt:%1d", init_data->q_format);
	DBF_HEX(init_data->adapter_name, 8);
	DBF_EVENT("qpff%4x", init_data->qib_param_field_format);
	DBF_HEX(&init_data->qib_param_field, sizeof(void *));
	DBF_HEX(&init_data->input_slib_elements, sizeof(void *));
	DBF_HEX(&init_data->output_slib_elements, sizeof(void *));
	DBF_EVENT("niq:%1d noq:%1d", init_data->no_input_qs,
		  init_data->no_output_qs);
	DBF_HEX(&init_data->input_handler, sizeof(void *));
	DBF_HEX(&init_data->output_handler, sizeof(void *));
	DBF_HEX(&init_data->int_parm, sizeof(long));
	DBF_HEX(&init_data->flags, sizeof(long));
	DBF_HEX(&init_data->input_sbal_addr_array, sizeof(void *));
	DBF_HEX(&init_data->output_sbal_addr_array, sizeof(void *));
	DBF_EVENT("irq:%8lx", (unsigned long)irq_ptr);

	/* allocate trace view for the interface */
	snprintf(text, 20, "qdio_%s", dev_name(&init_data->cdev->dev));
	irq_ptr->debug_area = debug_register(text, 2, 1, 16);
	debug_register_view(irq_ptr->debug_area, &debug_hex_ascii_view);
	debug_set_level(irq_ptr->debug_area, DBF_WARN);
	DBF_DEV_EVENT(DBF_ERR, irq_ptr, "dbf created");
}

static int qstat_show(struct seq_file *m, void *v)
{
	unsigned char state;
	struct qdio_q *q = m->private;
	int i;

	if (!q)
		return 0;

	seq_printf(m, "DSCI: %d   nr_used: %d\n",
		   *(u32 *)q->irq_ptr->dsci, atomic_read(&q->nr_buf_used));
	seq_printf(m, "ftc: %d  last_move: %d\n", q->first_to_check, q->last_move);
	seq_printf(m, "polling: %d  ack start: %d  ack count: %d\n",
		   q->u.in.polling, q->u.in.ack_start, q->u.in.ack_count);
	seq_printf(m, "slsb buffer states:\n");
	seq_printf(m, "|0      |8      |16     |24     |32     |40     |48     |56  63|\n");

	for (i = 0; i < QDIO_MAX_BUFFERS_PER_Q; i++) {
		debug_get_buf_state(q, i, &state);
		switch (state) {
		case SLSB_P_INPUT_NOT_INIT:
		case SLSB_P_OUTPUT_NOT_INIT:
			seq_printf(m, "N");
			break;
		case SLSB_P_INPUT_PRIMED:
		case SLSB_CU_OUTPUT_PRIMED:
			seq_printf(m, "+");
			break;
		case SLSB_P_INPUT_ACK:
			seq_printf(m, "A");
			break;
		case SLSB_P_INPUT_ERROR:
		case SLSB_P_OUTPUT_ERROR:
			seq_printf(m, "x");
			break;
		case SLSB_CU_INPUT_EMPTY:
		case SLSB_P_OUTPUT_EMPTY:
			seq_printf(m, "-");
			break;
		case SLSB_P_INPUT_HALTED:
		case SLSB_P_OUTPUT_HALTED:
			seq_printf(m, ".");
			break;
		default:
			seq_printf(m, "?");
		}
		if (i == 63)
			seq_printf(m, "\n");
	}
	seq_printf(m, "\n");
	seq_printf(m, "|64     |72     |80     |88     |96     |104    |112    |   127|\n");
	return 0;
}

static ssize_t qstat_seq_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *off)
{
	struct seq_file *seq = file->private_data;
	struct qdio_q *q = seq->private;

	if (!q)
		return 0;
	if (q->is_input_q)
		xchg(q->irq_ptr->dsci, 1);
	local_bh_disable();
	tasklet_schedule(&q->tasklet);
	local_bh_enable();
	return count;
}

static int qstat_seq_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, qstat_show,
			   filp->f_path.dentry->d_inode->i_private);
}

static const struct file_operations debugfs_fops = {
	.owner	 = THIS_MODULE,
	.open	 = qstat_seq_open,
	.read	 = seq_read,
	.write	 = qstat_seq_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

static char *qperf_names[] = {
	"Assumed adapter interrupts",
	"QDIO interrupts",
	"Requested PCIs",
	"Inbound tasklet runs",
	"Inbound tasklet resched",
	"Inbound tasklet resched2",
	"Outbound tasklet runs",
	"SIGA read",
	"SIGA write",
	"SIGA sync",
	"Inbound calls",
	"Inbound handler",
	"Inbound stop_polling",
	"Inbound queue full",
	"Outbound calls",
	"Outbound handler",
	"Outbound fast_requeue",
	"Outbound target_full",
	"QEBSM eqbs",
	"QEBSM eqbs partial",
	"QEBSM sqbs",
	"QEBSM sqbs partial"
};

static int qperf_show(struct seq_file *m, void *v)
{
	struct qdio_irq *irq_ptr = m->private;
	unsigned int *stat;
	int i;

	if (!irq_ptr)
		return 0;
	if (!irq_ptr->perf_stat_enabled) {
		seq_printf(m, "disabled\n");
		return 0;
	}
	stat = (unsigned int *)&irq_ptr->perf_stat;

	for (i = 0; i < ARRAY_SIZE(qperf_names); i++)
		seq_printf(m, "%26s:\t%u\n",
			   qperf_names[i], *(stat + i));
	return 0;
}

static ssize_t qperf_seq_write(struct file *file, const char __user *ubuf,
			       size_t count, loff_t *off)
{
	struct seq_file *seq = file->private_data;
	struct qdio_irq *irq_ptr = seq->private;
	unsigned long val;
	char buf[8];
	int ret;

	if (!irq_ptr)
		return 0;
	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(&buf, ubuf, count))
		return -EFAULT;
	buf[count] = 0;

	ret = strict_strtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	switch (val) {
	case 0:
		irq_ptr->perf_stat_enabled = 0;
		memset(&irq_ptr->perf_stat, 0, sizeof(irq_ptr->perf_stat));
		break;
	case 1:
		irq_ptr->perf_stat_enabled = 1;
		break;
	}
	return count;
}

static int qperf_seq_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, qperf_show,
			   filp->f_path.dentry->d_inode->i_private);
}

static struct file_operations debugfs_perf_fops = {
	.owner	 = THIS_MODULE,
	.open	 = qperf_seq_open,
	.read	 = seq_read,
	.write	 = qperf_seq_write,
	.llseek  = seq_lseek,
	.release = single_release,
};
static void setup_debugfs_entry(struct qdio_q *q, struct ccw_device *cdev)
{
	char name[QDIO_DEBUGFS_NAME_LEN];

	snprintf(name, QDIO_DEBUGFS_NAME_LEN, "%s_%d",
		 q->is_input_q ? "input" : "output",
		 q->nr);
	q->debugfs_q = debugfs_create_file(name, S_IFREG | S_IRUGO | S_IWUSR,
				q->irq_ptr->debugfs_dev, q, &debugfs_fops);
	if (IS_ERR(q->debugfs_q))
		q->debugfs_q = NULL;
}

void qdio_setup_debug_entries(struct qdio_irq *irq_ptr, struct ccw_device *cdev)
{
	struct qdio_q *q;
	int i;

	irq_ptr->debugfs_dev = debugfs_create_dir(dev_name(&cdev->dev),
						  debugfs_root);
	if (IS_ERR(irq_ptr->debugfs_dev))
		irq_ptr->debugfs_dev = NULL;

	irq_ptr->debugfs_perf = debugfs_create_file("statistics",
				S_IFREG | S_IRUGO | S_IWUSR,
				irq_ptr->debugfs_dev, irq_ptr,
				&debugfs_perf_fops);
	if (IS_ERR(irq_ptr->debugfs_perf))
		irq_ptr->debugfs_perf = NULL;

	for_each_input_queue(irq_ptr, q, i)
		setup_debugfs_entry(q, cdev);
	for_each_output_queue(irq_ptr, q, i)
		setup_debugfs_entry(q, cdev);
}

void qdio_shutdown_debug_entries(struct qdio_irq *irq_ptr, struct ccw_device *cdev)
{
	struct qdio_q *q;
	int i;

	for_each_input_queue(irq_ptr, q, i)
		debugfs_remove(q->debugfs_q);
	for_each_output_queue(irq_ptr, q, i)
		debugfs_remove(q->debugfs_q);
	debugfs_remove(irq_ptr->debugfs_perf);
	debugfs_remove(irq_ptr->debugfs_dev);
}

int __init qdio_debug_init(void)
{
	debugfs_root = debugfs_create_dir("qdio", NULL);

	qdio_dbf_setup = debug_register("qdio_setup", 16, 1, 16);
	debug_register_view(qdio_dbf_setup, &debug_hex_ascii_view);
	debug_set_level(qdio_dbf_setup, DBF_INFO);
	DBF_EVENT("dbf created\n");

	qdio_dbf_error = debug_register("qdio_error", 4, 1, 16);
	debug_register_view(qdio_dbf_error, &debug_hex_ascii_view);
	debug_set_level(qdio_dbf_error, DBF_INFO);
	DBF_ERROR("dbf created\n");
	return 0;
}

void qdio_debug_exit(void)
{
	debugfs_remove(debugfs_root);
	if (qdio_dbf_setup)
		debug_unregister(qdio_dbf_setup);
	if (qdio_dbf_error)
		debug_unregister(qdio_dbf_error);
}

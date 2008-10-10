/*
 *  drivers/s390/cio/qdio_debug.c
 *
 *  Copyright IBM Corp. 2008
 *
 *  Author: Jan Glauber (jang@linux.vnet.ibm.com)
 */
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <asm/qdio.h>
#include <asm/debug.h>
#include "qdio_debug.h"
#include "qdio.h"

debug_info_t *qdio_dbf_setup;
debug_info_t *qdio_dbf_trace;

static struct dentry *debugfs_root;
#define MAX_DEBUGFS_QUEUES	32
static struct dentry *debugfs_queues[MAX_DEBUGFS_QUEUES] = { NULL };
static DEFINE_MUTEX(debugfs_mutex);

void qdio_allocate_do_dbf(struct qdio_initialize *init_data)
{
	char dbf_text[20];

	sprintf(dbf_text, "qfmt:%x", init_data->q_format);
	QDIO_DBF_TEXT0(0, setup, dbf_text);
	QDIO_DBF_HEX0(0, setup, init_data->adapter_name, 8);
	sprintf(dbf_text, "qpff%4x", init_data->qib_param_field_format);
	QDIO_DBF_TEXT0(0, setup, dbf_text);
	QDIO_DBF_HEX0(0, setup, &init_data->qib_param_field, sizeof(void *));
	QDIO_DBF_HEX0(0, setup, &init_data->input_slib_elements, sizeof(void *));
	QDIO_DBF_HEX0(0, setup, &init_data->output_slib_elements, sizeof(void *));
	sprintf(dbf_text, "niq:%4x", init_data->no_input_qs);
	QDIO_DBF_TEXT0(0, setup, dbf_text);
	sprintf(dbf_text, "noq:%4x", init_data->no_output_qs);
	QDIO_DBF_TEXT0(0, setup, dbf_text);
	QDIO_DBF_HEX0(0, setup, &init_data->input_handler, sizeof(void *));
	QDIO_DBF_HEX0(0, setup, &init_data->output_handler, sizeof(void *));
	QDIO_DBF_HEX0(0, setup, &init_data->int_parm, sizeof(long));
	QDIO_DBF_HEX0(0, setup, &init_data->flags, sizeof(long));
	QDIO_DBF_HEX0(0, setup, &init_data->input_sbal_addr_array, sizeof(void *));
	QDIO_DBF_HEX0(0, setup, &init_data->output_sbal_addr_array, sizeof(void *));
}

static void qdio_unregister_dbf_views(void)
{
	if (qdio_dbf_setup)
		debug_unregister(qdio_dbf_setup);
	if (qdio_dbf_trace)
		debug_unregister(qdio_dbf_trace);
}

static int qdio_register_dbf_views(void)
{
	qdio_dbf_setup = debug_register("qdio_setup", QDIO_DBF_SETUP_PAGES,
					QDIO_DBF_SETUP_NR_AREAS,
					QDIO_DBF_SETUP_LEN);
	if (!qdio_dbf_setup)
		goto oom;
	debug_register_view(qdio_dbf_setup, &debug_hex_ascii_view);
	debug_set_level(qdio_dbf_setup, QDIO_DBF_SETUP_LEVEL);

	qdio_dbf_trace = debug_register("qdio_trace", QDIO_DBF_TRACE_PAGES,
					QDIO_DBF_TRACE_NR_AREAS,
					QDIO_DBF_TRACE_LEN);
	if (!qdio_dbf_trace)
		goto oom;
	debug_register_view(qdio_dbf_trace, &debug_hex_ascii_view);
	debug_set_level(qdio_dbf_trace, QDIO_DBF_TRACE_LEVEL);
	return 0;
oom:
	qdio_unregister_dbf_views();
	return -ENOMEM;
}

static int qstat_show(struct seq_file *m, void *v)
{
	unsigned char state;
	struct qdio_q *q = m->private;
	int i;

	if (!q)
		return 0;

	seq_printf(m, "device state indicator: %d\n", *q->irq_ptr->dsci);
	seq_printf(m, "nr_used: %d\n", atomic_read(&q->nr_buf_used));
	seq_printf(m, "ftc: %d\n", q->first_to_check);
	seq_printf(m, "last_move_ftc: %d\n", q->last_move_ftc);
	seq_printf(m, "polling: %d\n", q->u.in.polling);
	seq_printf(m, "slsb buffer states:\n");

	qdio_siga_sync_q(q);
	for (i = 0; i < QDIO_MAX_BUFFERS_PER_Q; i++) {
		get_buf_state(q, i, &state);
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

static void get_queue_name(struct qdio_q *q, struct ccw_device *cdev, char *name)
{
	memset(name, 0, sizeof(name));
	sprintf(name, "%s", dev_name(&cdev->dev));
	if (q->is_input_q)
		sprintf(name + strlen(name), "_input");
	else
		sprintf(name + strlen(name), "_output");
	sprintf(name + strlen(name), "_%d", q->nr);
}

static void remove_debugfs_entry(struct qdio_q *q)
{
	int i;

	for (i = 0; i < MAX_DEBUGFS_QUEUES; i++) {
		if (!debugfs_queues[i])
			continue;
		if (debugfs_queues[i]->d_inode->i_private == q) {
			debugfs_remove(debugfs_queues[i]);
			debugfs_queues[i] = NULL;
		}
	}
}

static struct file_operations debugfs_fops = {
	.owner	 = THIS_MODULE,
	.open	 = qstat_seq_open,
	.read	 = seq_read,
	.write	 = qstat_seq_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

static void setup_debugfs_entry(struct qdio_q *q, struct ccw_device *cdev)
{
	int i = 0;
	char name[40];

	while (debugfs_queues[i] != NULL) {
		i++;
		if (i >= MAX_DEBUGFS_QUEUES)
			return;
	}
	get_queue_name(q, cdev, name);
	debugfs_queues[i] = debugfs_create_file(name, S_IFREG | S_IRUGO | S_IWUSR,
						debugfs_root, q, &debugfs_fops);
}

void qdio_setup_debug_entries(struct qdio_irq *irq_ptr, struct ccw_device *cdev)
{
	struct qdio_q *q;
	int i;

	mutex_lock(&debugfs_mutex);
	for_each_input_queue(irq_ptr, q, i)
		setup_debugfs_entry(q, cdev);
	for_each_output_queue(irq_ptr, q, i)
		setup_debugfs_entry(q, cdev);
	mutex_unlock(&debugfs_mutex);
}

void qdio_shutdown_debug_entries(struct qdio_irq *irq_ptr, struct ccw_device *cdev)
{
	struct qdio_q *q;
	int i;

	mutex_lock(&debugfs_mutex);
	for_each_input_queue(irq_ptr, q, i)
		remove_debugfs_entry(q);
	for_each_output_queue(irq_ptr, q, i)
		remove_debugfs_entry(q);
	mutex_unlock(&debugfs_mutex);
}

int __init qdio_debug_init(void)
{
	debugfs_root = debugfs_create_dir("qdio_queues", NULL);
	return qdio_register_dbf_views();
}

void qdio_debug_exit(void)
{
	debugfs_remove(debugfs_root);
	qdio_unregister_dbf_views();
}

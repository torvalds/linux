// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author:
 *	Cerf Yu <cerf.yu@rock-chips.com>
 *	Huang Lee <Putin.li@rock-chips.com>
 */

#define pr_fmt(fmt) "rga_debugger: " fmt

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/syscalls.h>
#include <linux/debugfs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "rga.h"
#include "rga_debugger.h"
#include "rga_drv.h"
#include "rga_mm.h"
#include "rga_common.h"

#define RGA_DEBUGGER_ROOT_NAME "rkrga"

#define STR_ENABLE(en) (en ? "EN" : "DIS")

int RGA_DEBUG_REG;
int RGA_DEBUG_MSG;
int RGA_DEBUG_TIME;
int RGA_DEBUG_INT_FLAG;
int RGA_DEBUG_MM;
int RGA_DEBUG_CHECK_MODE;
int RGA_DEBUG_NONUSE;
int RGA_DEBUG_DEBUG_MODE;
int RGA_DEBUG_DUMP_IMAGE;

#ifdef CONFIG_NO_GKI
static char g_dump_path[100] = "/data";
#endif

static int rga_debug_show(struct seq_file *m, void *data)
{
	seq_printf(m, "REG [%s]\n"
		 "MSG [%s]\n"
		 "TIME [%s]\n"
		 "INT [%s]\n"
		 "MM [%s]\n"
		 "CHECK [%s]\n"
		 "STOP [%s]\n",
		 STR_ENABLE(RGA_DEBUG_REG),
		 STR_ENABLE(RGA_DEBUG_MSG),
		 STR_ENABLE(RGA_DEBUG_TIME),
		 STR_ENABLE(RGA_DEBUG_INT_FLAG),
		 STR_ENABLE(RGA_DEBUG_MM),
		 STR_ENABLE(RGA_DEBUG_CHECK_MODE),
		 STR_ENABLE(RGA_DEBUG_NONUSE));

	seq_puts(m, "\nhelp:\n");
	seq_puts(m, " 'echo reg > debug' to enable/disable register log printing.\n");
	seq_puts(m, " 'echo msg > debug' to enable/disable message log printing.\n");
	seq_puts(m, " 'echo time > debug' to enable/disable time log printing.\n");
	seq_puts(m, " 'echo int > debug' to enable/disable interruppt log printing.\n");
	seq_puts(m, " 'echo mm > debug' to enable/disable memory manager log printing.\n");
	seq_puts(m, " 'echo check > debug' to enable/disable check mode.\n");
	seq_puts(m, " 'echo stop > debug' to enable/disable stop using hardware\n");

	return 0;
}

static ssize_t rga_debug_write(struct file *file, const char __user *ubuf,
				 size_t len, loff_t *offp)
{
	char buf[14];

	if (len > sizeof(buf) - 1)
		return -EINVAL;
	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;
	buf[len - 1] = '\0';

	if (strncmp(buf, "reg", 4) == 0) {
		if (RGA_DEBUG_REG) {
			RGA_DEBUG_REG = 0;
			pr_info("close rga reg!\n");
		} else {
			RGA_DEBUG_REG = 1;
			pr_info("open rga reg!\n");
		}
	} else if (strncmp(buf, "msg", 3) == 0) {
		if (RGA_DEBUG_MSG) {
			RGA_DEBUG_MSG = 0;
			pr_info("close rga test MSG!\n");
		} else {
			RGA_DEBUG_MSG = 1;
			pr_info("open rga test MSG!\n");
		}
	} else if (strncmp(buf, "time", 4) == 0) {
		if (RGA_DEBUG_TIME) {
			RGA_DEBUG_TIME = 0;
			pr_info("close rga test time!\n");
		} else {
			RGA_DEBUG_TIME = 1;
			pr_info("open rga test time!\n");
		}
	} else if (strncmp(buf, "int", 3) == 0) {
		if (RGA_DEBUG_INT_FLAG) {
			RGA_DEBUG_INT_FLAG = 0;
			pr_info("close inturrupt MSG!\n");
		} else {
			RGA_DEBUG_INT_FLAG = 1;
			pr_info("open inturrupt MSG!\n");
		}
	} else if (strncmp(buf, "mm", 2) == 0) {
		if (RGA_DEBUG_MM) {
			RGA_DEBUG_MM = 0;
			pr_info("close rga mm log!\n");
		} else {
			RGA_DEBUG_MM = 1;
			pr_info("open rga mm log!\n");
		}
	} else if (strncmp(buf, "check", 5) == 0) {
		if (RGA_DEBUG_CHECK_MODE) {
			RGA_DEBUG_CHECK_MODE = 0;
			pr_info("close rga check flag!\n");
		} else {
			RGA_DEBUG_CHECK_MODE = 1;
			pr_info("open rga check flag!\n");
		}
	} else if (strncmp(buf, "stop", 4) == 0) {
		if (RGA_DEBUG_NONUSE) {
			RGA_DEBUG_NONUSE = 0;
			pr_info("using rga hardware!\n");
		} else {
			RGA_DEBUG_NONUSE = 1;
			pr_info("stop using rga hardware!\n");
		}
	} else if (strncmp(buf, "debug", 3) == 0) {
		if (RGA_DEBUG_DEBUG_MODE) {
			RGA_DEBUG_REG = 0;
			RGA_DEBUG_MSG = 0;
			RGA_DEBUG_TIME = 0;
			RGA_DEBUG_INT_FLAG = 0;

			RGA_DEBUG_DEBUG_MODE = 0;
			pr_info("close debug mode!\n");
		} else {
			RGA_DEBUG_REG = 1;
			RGA_DEBUG_MSG = 1;
			RGA_DEBUG_TIME = 1;
			RGA_DEBUG_INT_FLAG = 1;

			RGA_DEBUG_DEBUG_MODE = 1;
			pr_info("open debug mode!\n");
		}
	} else if (strncmp(buf, "slt", 3) == 0) {
		pr_err("Null");
	}

	return len;
}

static int rga_version_show(struct seq_file *m, void *data)
{
	seq_printf(m, "%s: v%s\n", DRIVER_DESC, DRIVER_VERSION);

	return 0;
}

static int rga_load_show(struct seq_file *m, void *data)
{
	struct rga_scheduler_t *scheduler = NULL;
	struct rga_session_manager *session_manager = NULL;
	struct rga_session *session = NULL;
	unsigned long flags;
	int id = 0;
	int i;
	int load;
	u32 busy_time_total;

	session_manager = rga_drvdata->session_manager;

	seq_printf(m, "num of scheduler = %d\n", rga_drvdata->num_of_scheduler);
	seq_printf(m, "================= load ==================\n");

	for (i = 0; i < rga_drvdata->num_of_scheduler; i++) {
		scheduler = rga_drvdata->scheduler[i];

		seq_printf(m, "scheduler[%d]: %s\n",
			i, dev_driver_string(scheduler->dev));

		spin_lock_irqsave(&scheduler->irq_lock, flags);

		busy_time_total = scheduler->timer.busy_time_record;

		spin_unlock_irqrestore(&scheduler->irq_lock, flags);

		load = (busy_time_total * 100 / RGA_LOAD_INTERVAL_US);
		if (load > 100)
			load = 100;

		seq_printf(m, "\t load = %d%%\n", load);
		seq_printf(m, "-----------------------------------\n");
	}

	mutex_lock(&session_manager->lock);

	idr_for_each_entry(&session_manager->ctx_id_idr, session, id)
		seq_printf(m, "\t process %d: pid = %d, name: %s\n", id,
			session->tgid, session->pname);

	mutex_unlock(&session_manager->lock);

	return 0;
}

static int rga_scheduler_show(struct seq_file *m, void *data)
{
	struct rga_scheduler_t *scheduler = NULL;
	int i;

	seq_printf(m, "num of scheduler = %d\n", rga_drvdata->num_of_scheduler);
	seq_printf(m, "===================================\n");

	for (i = 0; i < rga_drvdata->num_of_scheduler; i++) {
		scheduler = rga_drvdata->scheduler[i];

		seq_printf(m, "scheduler[%d]: %s\n",
			i, dev_driver_string(scheduler->dev));
		seq_printf(m, "-----------------------------------\n");
		seq_printf(m, "pd_ref = %d\n", scheduler->pd_refcount);
	}

	return 0;
}

static int rga_mm_session_show(struct seq_file *m, void *data)
{
	int id;
	struct rga_mm *mm_session = NULL;
	struct rga_internal_buffer *dump_buffer;

	mm_session = rga_drvdata->mm;

	mutex_lock(&mm_session->lock);

	seq_puts(m, "rga_mm dump:\n");
	seq_printf(m, "buffer count = %d\n", mm_session->buffer_count);
	seq_puts(m, "===============================================================\n");

	idr_for_each_entry(&mm_session->memory_idr, dump_buffer, id) {
		seq_printf(m, "handle = %d refcount = %d mm_flag = 0x%x	tgid = %d\n",
			   dump_buffer->handle, kref_read(&dump_buffer->refcount),
			   dump_buffer->mm_flag, dump_buffer->session->tgid);

		switch (dump_buffer->type) {
		case RGA_DMA_BUFFER:
		case RGA_DMA_BUFFER_PTR:
			if (rga_mm_is_invalid_dma_buffer(dump_buffer->dma_buffer))
				break;

			seq_puts(m, "dma_buffer:\n");
			seq_printf(m, "\t dma_buf = %p, iova = 0x%lxsgt = 0x%p, size = %ld, map_core = 0x%x\n",
				   dump_buffer->dma_buffer->dma_buf,
				   (unsigned long)dump_buffer->dma_buffer->iova,
				   dump_buffer->dma_buffer->sgt,
				   dump_buffer->dma_buffer->size,
				   dump_buffer->dma_buffer->scheduler->core);

			if (dump_buffer->mm_flag & RGA_MEM_PHYSICAL_CONTIGUOUS)
				seq_printf(m, "\t is contiguous, pa = 0x%lx\n",
					   (unsigned long)dump_buffer->phys_addr);

			break;
		case RGA_VIRTUAL_ADDRESS:
			if (dump_buffer->virt_addr == NULL)
				break;
			seq_puts(m, "virtual address:\n");
			seq_printf(m, "\t va = 0x%lx, pages = 0x%p, size = %ld\n",
				   (unsigned long)dump_buffer->virt_addr->addr,
				   dump_buffer->virt_addr->pages,
				   dump_buffer->virt_addr->size);

			if (rga_mm_is_invalid_dma_buffer(dump_buffer->dma_buffer))
				break;

			seq_printf(m, "\t iova = 0x%lx, offset = 0x%lx, sgt = 0x%p, size = %ld, map_core = 0x%x\n",
				   (unsigned long)dump_buffer->dma_buffer->iova,
				   (unsigned long)dump_buffer->dma_buffer->offset,
				   dump_buffer->dma_buffer->sgt,
				   dump_buffer->dma_buffer->size,
				   dump_buffer->dma_buffer->scheduler->core);

			if (dump_buffer->mm_flag & RGA_MEM_PHYSICAL_CONTIGUOUS)
				seq_printf(m, "\t is contiguous, pa = 0x%lx\n",
					   (unsigned long)dump_buffer->phys_addr);

			break;
		case RGA_PHYSICAL_ADDRESS:
			seq_puts(m, "physical address:\n");
			seq_printf(m, "\t pa = 0x%lx\n", (unsigned long)dump_buffer->phys_addr);
			break;
		default:
			seq_puts(m, "Illegal external buffer!\n");
			break;
		}

		seq_puts(m, "---------------------------------------------------------------\n");
	}
	mutex_unlock(&mm_session->lock);

	return 0;
}

static int rga_request_manager_show(struct seq_file *m, void *data)
{
	int id, i;
	struct rga_pending_request_manager *request_manager;
	struct rga_request *request;
	struct rga_req *task_list;
	unsigned long flags;
	int task_count = 0;
	int finished_task_count = 0, failed_task_count = 0;

	request_manager = rga_drvdata->pend_request_manager;

	seq_puts(m, "rga internal request dump:\n");
	seq_printf(m, "request count = %d\n", request_manager->request_count);
	seq_puts(m, "===============================================================\n");

	mutex_lock(&request_manager->lock);

	idr_for_each_entry(&request_manager->request_idr, request, id) {
		seq_printf(m, "------------------ request: %d ------------------\n", request->id);

		spin_lock_irqsave(&request->lock, flags);

		task_count = request->task_count;
		finished_task_count = request->finished_task_count;
		failed_task_count = request->failed_task_count;
		task_list = request->task_list;

		spin_unlock_irqrestore(&request->lock, flags);

		if (task_list == NULL) {
			seq_puts(m, "\t can not find task list from id\n");
			continue;
		}

		seq_printf(m, "\t set cmd num: %d, finish job: %d, failed job: %d, flags = 0x%x, ref = %d\n",
			   task_count, finished_task_count, failed_task_count,
			   request->flags, kref_read(&request->refcount));

		seq_puts(m, "\t cmd dump:\n\n");

		for (i = 0; i < request->task_count; i++)
			rga_request_task_debug_info(m, &(task_list[i]));
	}

	mutex_unlock(&request_manager->lock);

	return 0;
}

#ifdef CONFIG_NO_GKI
static int rga_dump_path_show(struct seq_file *m, void *data)
{
	seq_printf(m, "dump path: %s\n", g_dump_path);

	return 0;
}

static ssize_t rga_dump_path_write(struct file *file, const char __user *ubuf,
				    size_t len, loff_t *offp)
{
	char buf[100];

	if (len > sizeof(buf) - 1)
		return -EINVAL;
	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;
	buf[len - 1] = '\0';

	snprintf(g_dump_path, sizeof(buf), "%s", buf);
	pr_info("dump path change to: %s\n", g_dump_path);

	return len;
}

static int rga_dump_image_show(struct seq_file *m, void *data)
{
	seq_printf(m, "dump image count: %d\n", RGA_DEBUG_DUMP_IMAGE);

	return 0;
}

static ssize_t rga_dump_image_write(struct file *file, const char __user *ubuf,
				    size_t len, loff_t *offp)
{
	int ret;
	int dump_count = 0;
	char buf[14];

	if (len > sizeof(buf) - 1)
		return -EINVAL;
	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;
	buf[len - 1] = '\0';

	ret = kstrtoint(buf, 10, &dump_count);
	if (ret) {
		pr_err("Failed to parse str[%s]\n", buf);
		return -EFAULT;
	}

	if (dump_count <= 0) {
		pr_err("dump_image count is invalid [%d]!\n", dump_count);
		return -EINVAL;
	}

	RGA_DEBUG_DUMP_IMAGE = dump_count;
	pr_info("dump image %d\n", RGA_DEBUG_DUMP_IMAGE);

	return len;
}
#endif /* #ifdef CONFIG_NO_GKI */

static int rga_hardware_show(struct seq_file *m, void *data)
{
	struct rga_scheduler_t *scheduler = NULL;
	const struct rga_hw_data *hw_data = NULL;
	int i;

	seq_puts(m, "===================================\n");

	for (i = 0; i < rga_drvdata->num_of_scheduler; i++) {
		scheduler = rga_drvdata->scheduler[i];
		hw_data = scheduler->data;

		seq_printf(m, "%s, core %d: version: %s\n",
			   dev_driver_string(scheduler->dev),
			   scheduler->core, scheduler->version.str);
		seq_printf(m, "input range: %dx%d ~ %dx%d\n",
			   hw_data->input_range.min.width, hw_data->input_range.min.height,
			   hw_data->input_range.max.width, hw_data->input_range.max.height);
		seq_printf(m, "output range: %dx%d ~ %dx%d\n",
			   hw_data->output_range.min.width, hw_data->output_range.min.height,
			   hw_data->output_range.max.width, hw_data->output_range.max.height);
		seq_printf(m, "scale limit: 1/%d ~ %d\n",
			   (1 << hw_data->max_downscale_factor),
			   (1 << hw_data->max_upscale_factor));
		seq_printf(m, "byte_stride_align: %d\n", hw_data->byte_stride_align);
		seq_printf(m, "max_byte_stride: %d\n", hw_data->max_byte_stride);
		seq_printf(m, "csc: RGB2YUV 0x%x YUV2RGB 0x%x\n",
			   hw_data->csc_r2y_mode, hw_data->csc_y2r_mode);
		seq_printf(m, "feature: 0x%x\n", hw_data->feature);
		seq_printf(m, "mmu: %s\n", rga_get_mmu_type_str(hw_data->mmu));
		seq_puts(m, "-----------------------------------\n");
	}

	return 0;
}

static struct rga_debugger_list rga_debugger_root_list[] = {
	{"debug", rga_debug_show, rga_debug_write, NULL},
	{"driver_version", rga_version_show, NULL, NULL},
	{"load", rga_load_show, NULL, NULL},
	{"scheduler_status", rga_scheduler_show, NULL, NULL},
	{"mm_session", rga_mm_session_show, NULL, NULL},
	{"request_manager", rga_request_manager_show, NULL, NULL},
#ifdef CONFIG_NO_GKI
	{"dump_path", rga_dump_path_show, rga_dump_path_write, NULL},
	{"dump_image", rga_dump_image_show, rga_dump_image_write, NULL},
#endif
	{"hardware", rga_hardware_show, NULL, NULL},
};

static ssize_t rga_debugger_write(struct file *file, const char __user *ubuf,
				 size_t len, loff_t *offp)
{
	struct seq_file *priv = file->private_data;
	struct rga_debugger_node *node = priv->private;

	if (node->info_ent->write)
		return node->info_ent->write(file, ubuf, len, offp);
	else
		return len;
}

#ifdef CONFIG_ROCKCHIP_RGA_DEBUG_FS
static int rga_debugfs_open(struct inode *inode, struct file *file)
{
	struct rga_debugger_node *node = inode->i_private;

	return single_open(file, node->info_ent->show, node);
}

static const struct file_operations rga_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = rga_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = rga_debugger_write,
};

static int rga_debugfs_remove_files(struct rga_debugger *debugger)
{
	struct rga_debugger_node *pos, *q;
	struct list_head *entry_list;

	mutex_lock(&debugger->debugfs_lock);

	/* Delete debugfs entry list */
	entry_list = &debugger->debugfs_entry_list;
	list_for_each_entry_safe(pos, q, entry_list, list) {
		if (pos->dent == NULL)
			continue;
		list_del(&pos->list);
		kfree(pos);
		pos = NULL;
	}

	/* Delete all debugfs node in this directory */
	debugfs_remove_recursive(debugger->debugfs_dir);
	debugger->debugfs_dir = NULL;

	mutex_unlock(&debugger->debugfs_lock);

	return 0;
}

static int rga_debugfs_create_files(const struct rga_debugger_list *files,
					int count, struct dentry *root,
					struct rga_debugger *debugger)
{
	int i;
	struct dentry *ent;
	struct rga_debugger_node *tmp;

	for (i = 0; i < count; i++) {
		tmp = kmalloc(sizeof(struct rga_debugger_node), GFP_KERNEL);
		if (tmp == NULL) {
			pr_err("Cannot alloc node path /sys/kernel/debug/%pd/%s\n",
				 root, files[i].name);
			goto MALLOC_FAIL;
		}

		tmp->info_ent = &files[i];
		tmp->debugger = debugger;

		ent = debugfs_create_file(files[i].name, S_IFREG | S_IRUGO,
					 root, tmp, &rga_debugfs_fops);
		if (!ent) {
			pr_err("Cannot create /sys/kernel/debug/%pd/%s\n", root,
				 files[i].name);
			goto CREATE_FAIL;
		}

		tmp->dent = ent;

		mutex_lock(&debugger->debugfs_lock);
		list_add_tail(&tmp->list, &debugger->debugfs_entry_list);
		mutex_unlock(&debugger->debugfs_lock);
	}

	return 0;

CREATE_FAIL:
	kfree(tmp);
MALLOC_FAIL:
	rga_debugfs_remove_files(debugger);

	return -1;
}

int rga_debugfs_remove(void)
{
	struct rga_debugger *debugger;

	debugger = rga_drvdata->debugger;

	rga_debugfs_remove_files(debugger);

	return 0;
}

int rga_debugfs_init(void)
{
	int ret;
	struct rga_debugger *debugger;

	debugger = rga_drvdata->debugger;

	debugger->debugfs_dir =
		debugfs_create_dir(RGA_DEBUGGER_ROOT_NAME, NULL);
	if (IS_ERR_OR_NULL(debugger->debugfs_dir)) {
		pr_err("failed on mkdir /sys/kernel/debug/%s\n",
			 RGA_DEBUGGER_ROOT_NAME);
		debugger->debugfs_dir = NULL;
		return -EIO;
	}

	ret = rga_debugfs_create_files(rga_debugger_root_list, ARRAY_SIZE(rga_debugger_root_list),
					 debugger->debugfs_dir, debugger);
	if (ret) {
		pr_err("Could not install rga_debugger_root_list debugfs\n");
		goto CREATE_FAIL;
	}

	return 0;

CREATE_FAIL:
	rga_debugfs_remove();

	return ret;
}
#endif /* #ifdef CONFIG_ROCKCHIP_RGA_DEBUG_FS */

#ifdef CONFIG_ROCKCHIP_RGA_PROC_FS
static int rga_procfs_open(struct inode *inode, struct file *file)
{
	struct rga_debugger_node *node = PDE_DATA(inode);

	return single_open(file, node->info_ent->show, node);
}

static const struct proc_ops rga_procfs_fops = {
	.proc_open = rga_procfs_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = rga_debugger_write,
};

static int rga_procfs_remove_files(struct rga_debugger *debugger)
{
	struct rga_debugger_node *pos, *q;
	struct list_head *entry_list;

	mutex_lock(&debugger->procfs_lock);

	/* Delete procfs entry list */
	entry_list = &debugger->procfs_entry_list;
	list_for_each_entry_safe(pos, q, entry_list, list) {
		if (pos->pent == NULL)
			continue;
		list_del(&pos->list);
		kfree(pos);
		pos = NULL;
	}

	/* Delete all procfs node in this directory */
	proc_remove(debugger->procfs_dir);
	debugger->procfs_dir = NULL;

	mutex_unlock(&debugger->procfs_lock);

	return 0;
}

static int rga_procfs_create_files(const struct rga_debugger_list *files,
				 int count, struct proc_dir_entry *root,
				 struct rga_debugger *debugger)
{
	int i;
	struct proc_dir_entry *ent;
	struct rga_debugger_node *tmp;

	for (i = 0; i < count; i++) {
		tmp = kmalloc(sizeof(struct rga_debugger_node), GFP_KERNEL);
		if (tmp == NULL) {
			pr_err("Cannot alloc node path for /proc/%s/%s\n",
				 RGA_DEBUGGER_ROOT_NAME, files[i].name);
			goto MALLOC_FAIL;
		}

		tmp->info_ent = &files[i];
		tmp->debugger = debugger;

		ent = proc_create_data(files[i].name, S_IFREG | S_IRUGO,
					 root, &rga_procfs_fops, tmp);
		if (!ent) {
			pr_err("Cannot create /proc/%s/%s\n",
				 RGA_DEBUGGER_ROOT_NAME, files[i].name);
			goto CREATE_FAIL;
		}

		tmp->pent = ent;

		mutex_lock(&debugger->procfs_lock);
		list_add_tail(&tmp->list, &debugger->procfs_entry_list);
		mutex_unlock(&debugger->procfs_lock);
	}

	return 0;

CREATE_FAIL:
	kfree(tmp);
MALLOC_FAIL:
	rga_procfs_remove_files(debugger);
	return -1;
}

int rga_procfs_remove(void)
{
	struct rga_debugger *debugger;

	debugger = rga_drvdata->debugger;

	rga_procfs_remove_files(debugger);

	return 0;
}

int rga_procfs_init(void)
{
	int ret;
	struct rga_debugger *debugger;

	debugger = rga_drvdata->debugger;

	debugger->procfs_dir = proc_mkdir(RGA_DEBUGGER_ROOT_NAME, NULL);
	if (IS_ERR_OR_NULL(debugger->procfs_dir)) {
		pr_err("failed on mkdir /proc/%s\n", RGA_DEBUGGER_ROOT_NAME);
		debugger->procfs_dir = NULL;
		return -EIO;
	}

	ret = rga_procfs_create_files(rga_debugger_root_list, ARRAY_SIZE(rga_debugger_root_list),
					 debugger->procfs_dir, debugger);
	if (ret) {
		pr_err("Could not install rga_debugger_root_list procfs\n");
		goto CREATE_FAIL;
	}

	return 0;

CREATE_FAIL:
	rga_procfs_remove();

	return ret;
}
#endif /* #ifdef CONFIG_ROCKCHIP_RGA_PROC_FS */

void rga_request_task_debug_info(struct seq_file *m, struct rga_req *req)
{
	seq_printf(m, "\t\t rotate_mode = %d\n", req->rotate_mode);
	seq_printf(m, "\t\t src: y = %lx uv = %lx v = %lx aw = %d ah = %d vw = %d vh = %d\n",
		 (unsigned long)req->src.yrgb_addr, (unsigned long)req->src.uv_addr,
		 (unsigned long)req->src.v_addr, req->src.act_w, req->src.act_h,
		 req->src.vir_w, req->src.vir_h);
	seq_printf(m, "\t\t src: xoff = %d, yoff = %d, format = 0x%x, rd_mode = %d\n",
		req->src.x_offset, req->src.y_offset, req->src.format, req->src.rd_mode);

	if (req->pat.yrgb_addr != 0 || req->pat.uv_addr != 0
		|| req->pat.v_addr != 0) {
		seq_printf(m, "\t\t pat: y=%lx uv=%lx v=%lx aw=%d ah=%d vw=%d vh=%d\n",
			 (unsigned long)req->pat.yrgb_addr, (unsigned long)req->pat.uv_addr,
			 (unsigned long)req->pat.v_addr, req->pat.act_w, req->pat.act_h,
			 req->pat.vir_w, req->pat.vir_h);
		seq_printf(m, "\t\t xoff = %d yoff = %d, format = 0x%x, rd_mode = %d\n",
			req->pat.x_offset, req->pat.y_offset, req->pat.format, req->pat.rd_mode);
	}

	seq_printf(m, "\t\t dst: y=%lx uv=%lx v=%lx aw=%d ah=%d vw=%d vh=%d\n",
		 (unsigned long)req->dst.yrgb_addr, (unsigned long)req->dst.uv_addr,
		 (unsigned long)req->dst.v_addr, req->dst.act_w, req->dst.act_h,
		 req->dst.vir_w, req->dst.vir_h);
	seq_printf(m, "\t\t dst: xoff = %d, yoff = %d, format = 0x%x, rd_mode = %d\n",
		req->dst.x_offset, req->dst.y_offset, req->dst.format, req->dst.rd_mode);

	seq_printf(m, "\t\t mmu: mmu_flag=%x en=%x\n",
		req->mmu_info.mmu_flag, req->mmu_info.mmu_en);
	seq_printf(m, "\t\t alpha: rop_mode = %x\n", req->alpha_rop_mode);
	seq_printf(m, "\t\t yuv2rgb mode is %x\n", req->yuv2rgb_mode);
	seq_printf(m, "\t\t set core = %d, priority = %d, in_fence_fd = %d\n",
		req->core, req->priority, req->in_fence_fd);
}

void rga_cmd_print_debug_info(struct rga_req *req)
{
	pr_info("render_mode = %d, bitblit_mode=%d, rotate_mode = %d\n",
		req->render_mode, req->bsfilter_flag,
		req->rotate_mode);

	pr_info("src: y = %lx uv = %lx v = %lx aw = %d ah = %d vw = %d vh = %d\n",
		 (unsigned long)req->src.yrgb_addr,
		 (unsigned long)req->src.uv_addr,
		 (unsigned long)req->src.v_addr,
		 req->src.act_w, req->src.act_h,
		 req->src.vir_w, req->src.vir_h);
	pr_info("src: xoff = %d, yoff = %d, format = 0x%x, rd_mode = %d\n",
		req->src.x_offset, req->src.y_offset,
		 req->src.format, req->src.rd_mode);

	if (req->pat.yrgb_addr != 0 || req->pat.uv_addr != 0
		|| req->pat.v_addr != 0) {
		pr_info("pat: y=%lx uv=%lx v=%lx aw=%d ah=%d vw=%d vh=%d\n",
			 (unsigned long)req->pat.yrgb_addr,
			 (unsigned long)req->pat.uv_addr,
			 (unsigned long)req->pat.v_addr,
			 req->pat.act_w, req->pat.act_h,
			 req->pat.vir_w, req->pat.vir_h);
		pr_info("pat: xoff = %d yoff = %d, format = 0x%x, rd_mode = %d\n",
			req->pat.x_offset, req->pat.y_offset,
			req->pat.format, req->pat.rd_mode);
	}

	pr_info("dst: y=%lx uv=%lx v=%lx aw=%d ah=%d vw=%d vh=%d\n",
		 (unsigned long)req->dst.yrgb_addr,
		 (unsigned long)req->dst.uv_addr,
		 (unsigned long)req->dst.v_addr,
		 req->dst.act_w, req->dst.act_h,
		 req->dst.vir_w, req->dst.vir_h);
	pr_info("dst: xoff = %d, yoff = %d, format = 0x%x, rd_mode = %d\n",
		req->dst.x_offset, req->dst.y_offset,
		req->dst.format, req->dst.rd_mode);

	pr_info("mmu: mmu_flag=%x en=%x\n",
		req->mmu_info.mmu_flag, req->mmu_info.mmu_en);
	pr_info("alpha: rop_mode = %x\n", req->alpha_rop_mode);
	pr_info("yuv2rgb mode is %x\n", req->yuv2rgb_mode);
	pr_info("set core = %d, priority = %d, in_fence_fd = %d\n",
		req->core, req->priority, req->in_fence_fd);
}

void rga_dump_external_buffer(struct rga_external_buffer *buffer)
{
	pr_info("external: memory = 0x%lx, type = %s\n",
		(unsigned long)buffer->memory, rga_get_memory_type_str(buffer->type));
	pr_info("param: w = %d, h = %d, f = %s, size = %d\n",
		buffer->memory_parm.width, buffer->memory_parm.height,
		rga_get_format_name(buffer->memory_parm.format),
		buffer->memory_parm.size);
}

#ifdef CONFIG_NO_GKI
static int rga_dump_image_to_file(struct rga_internal_buffer *dump_buffer,
				  const char *channel_name,
				  int plane_id,
				  int core)
{
	char file_name[100];
	struct file *file;
	size_t size = 0;
	loff_t pos = 0;
	void *kvaddr = NULL;
	void *kvaddr_origin = NULL;

	switch (dump_buffer->type) {
	case RGA_DMA_BUFFER:
	case RGA_DMA_BUFFER_PTR:
		if (IS_ERR_OR_NULL(dump_buffer->dma_buffer->dma_buf)) {
			pr_err("Failed to dump dma_buf 0x%px\n",
			       dump_buffer->dma_buffer->dma_buf);
			return -EINVAL;
		}

		kvaddr = dma_buf_vmap(dump_buffer->dma_buffer->dma_buf);
		if (!kvaddr) {
			pr_err("can't vmap the dma buffer!\n");
			return -EINVAL;
		}

		kvaddr_origin = kvaddr;
		kvaddr += dump_buffer->dma_buffer->offset;
		break;
	case RGA_VIRTUAL_ADDRESS:
		kvaddr = vmap(dump_buffer->virt_addr->pages, dump_buffer->virt_addr->page_count,
			      VM_MAP, pgprot_writecombine(PAGE_KERNEL));
		if (!kvaddr) {
			pr_err("dump_vaddr vmap error!, 0x%lx\n",
			       (unsigned long)dump_buffer->virt_addr->addr);
			return -EFAULT;
		}

		kvaddr_origin = kvaddr;
		kvaddr += dump_buffer->virt_addr->offset;
		break;
	case RGA_PHYSICAL_ADDRESS:
		kvaddr = phys_to_virt(dump_buffer->phys_addr);
		break;
	default:
		pr_err("unsupported memory type[%x]\n", dump_buffer->type);
		return -EINVAL;
	}

	size = dump_buffer->size;

	if (kvaddr == NULL) {
		pr_err("dump addr is NULL!\n");
		return -EFAULT;
	}

	if (size <= 0) {
		pr_err("dump buffer size[%lx] is invalid!\n", (unsigned long)size);
		return -EFAULT;
	}

	if (dump_buffer->memory_parm.width == 0 &&
	    dump_buffer->memory_parm.height == 0)
		snprintf(file_name, 100, "%s/%d_core%d_%s_plane%d_%s_size%zu_%s.bin",
			 g_dump_path,
			 RGA_DEBUG_DUMP_IMAGE, core, channel_name, plane_id,
			 rga_get_memory_type_str(dump_buffer->type),
			 size,
			 rga_get_format_name(dump_buffer->memory_parm.format));
	else
		snprintf(file_name, 100, "%s/%d_core%d_%s_plane%d_%s_w%d_h%d_%s.bin",
			 g_dump_path,
			 RGA_DEBUG_DUMP_IMAGE, core, channel_name, plane_id,
			 rga_get_memory_type_str(dump_buffer->type),
			 dump_buffer->memory_parm.width,
			 dump_buffer->memory_parm.height,
			 rga_get_format_name(dump_buffer->memory_parm.format));

	file = filp_open(file_name, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (!IS_ERR(file)) {
		kernel_write(file, kvaddr, size, &pos);
		pr_info("dump image to: %s\n", file_name);
		fput(file);
	} else {
		pr_info("open %s failed\n", file_name);
	}

	switch (dump_buffer->type) {
	case RGA_DMA_BUFFER:
	case RGA_DMA_BUFFER_PTR:
		dma_buf_vunmap(dump_buffer->dma_buffer->dma_buf, kvaddr_origin);
		break;
	case RGA_VIRTUAL_ADDRESS:
		vunmap(kvaddr_origin);
		break;
	}

	return 0;
}

static inline void rga_dump_channel_image(struct rga_job_buffer *job_buffer,
					  const char *channel_name,
					  int core)
{
	if (job_buffer->y_addr)
		rga_dump_image_to_file(job_buffer->y_addr, channel_name, 0, core);
	if (job_buffer->uv_addr)
		rga_dump_image_to_file(job_buffer->uv_addr, channel_name, 1, core);
	if (job_buffer->v_addr)
		rga_dump_image_to_file(job_buffer->v_addr, channel_name, 2, core);
}

void rga_dump_job_image(struct rga_job *dump_job)
{
	rga_dump_channel_image(&dump_job->src_buffer, "src", dump_job->core);
	rga_dump_channel_image(&dump_job->src1_buffer, "src1", dump_job->core);
	rga_dump_channel_image(&dump_job->dst_buffer, "dst", dump_job->core);
	rga_dump_channel_image(&dump_job->els_buffer, "els", dump_job->core);

	if (RGA_DEBUG_DUMP_IMAGE > 0)
		RGA_DEBUG_DUMP_IMAGE--;
}
#endif /* #ifdef CONFIG_NO_GKI */

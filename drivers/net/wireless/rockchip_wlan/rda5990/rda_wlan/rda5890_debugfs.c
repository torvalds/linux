#include <linux/module.h>
#include <linux/dcache.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <net/iw_handler.h>

#include "rda5890_defs.h"
#include "rda5890_dev.h"

static struct dentry *rda5890_dir = NULL;

void dump_buf(char *data, size_t len)
{
    char temp_buf[64];
    size_t i, off = 0;

    memset(temp_buf, 0, 64);
    for (i=0;i<len;i++) {
        if(i%8 == 0) {
            sprintf(&temp_buf[off], "  ");
            off += 2;
        }
        sprintf(&temp_buf[off], "%02x ", data[i]);
        off += 3;
        if((i+1)%16 == 0) {
            RDA5890_DBGLAP(RDA5890_DA_ALL, RDA5890_DL_TRACE,
                "%s\n", temp_buf);
            memset(temp_buf, 0, 64);
            off = 0;
        }
    }
    RDA5890_DBGLAP(RDA5890_DA_ALL, RDA5890_DL_TRACE, "\n");
}

static int open_file_generic(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

#if 0
static ssize_t rda5890_write_file_dummy(struct file *file, const char __user *buf,
                                size_t count, loff_t *ppos)
{
        return -EINVAL;
}
#endif

static ssize_t rda5890_debug_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	//struct rda5890_private *priv = file->private_data;
	size_t pos = 0;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	ssize_t res;

	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		"%s\n", __func__);

	pos += snprintf(buf+pos, PAGE_SIZE - pos, "state = %s\n",
				"LWANG_TESTING");

	res = simple_read_from_buffer(userbuf, count, ppos, buf, pos);

	free_page(addr);
	return res;
}

static ssize_t rda5890_debug_write(struct file *file,
				const char __user *user_buf, size_t count,
				loff_t *ppos)
{
	//struct rda5890_private *priv = file->private_data;
	ssize_t ret;
	int p1, p2, p3, p4;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		"%s\n", __func__);

	if (copy_from_user(buf, user_buf, count)) {
		ret = -EFAULT;
		goto out_unlock;
	}
	ret = sscanf(buf, "%d %d %d %d", &p1, &p2, &p3, &p4);
	if (ret != 4) {
		ret = -EINVAL;
		goto out_unlock;
	}

	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		"input p1 = %d, p2 = %d, p3 = %d, p4 = %d\n",
		p1, p2, p3, p4);

	ret = count;
out_unlock:
	free_page(addr);
	return ret;
}

static ssize_t rda5890_debugarea_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	//struct rda5890_private *priv = file->private_data;
	size_t pos = 0;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	ssize_t res;

	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		"%s\n", __func__);

	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		"get debug_area = 0x%x\n",rda5890_dbg_area);

	pos += snprintf(buf+pos, PAGE_SIZE - pos, "%x\n",
				rda5890_dbg_area);

	res = simple_read_from_buffer(userbuf, count, ppos, buf, pos);

	free_page(addr);
	return res;
}

static ssize_t rda5890_debugarea_write(struct file *file,
				const char __user *user_buf, size_t count,
				loff_t *ppos)
{
	//struct rda5890_private *priv = file->private_data;
	ssize_t ret;
	int debug_area;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		"%s\n", __func__);

	if (copy_from_user(buf, user_buf, count)) {
		ret = -EFAULT;
		goto out_unlock;
	}
	ret = sscanf(buf, "%x", &debug_area);
	if (ret != 1) {
		ret = -EINVAL;
		goto out_unlock;
	}

    rda5890_dbg_area = debug_area;
	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		"set debug_area = 0x%x\n",rda5890_dbg_area);

	ret = count;
out_unlock:
	free_page(addr);
	return ret;
}

static ssize_t rda5890_debuglevel_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	//struct rda5890_private *priv = file->private_data;
	size_t pos = 0;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	ssize_t res;

	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		"%s\n", __func__);

	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		"get debug_level = 0x%x\n",rda5890_dbg_level);

	pos += snprintf(buf+pos, PAGE_SIZE - pos, "%x\n",
				rda5890_dbg_level);

	res = simple_read_from_buffer(userbuf, count, ppos, buf, pos);

	free_page(addr);
	return res;
}

static ssize_t rda5890_debuglevel_write(struct file *file,
				const char __user *user_buf, size_t count,
				loff_t *ppos)
{
	//struct rda5890_private *priv = file->private_data;
	ssize_t ret;
	int debug_level;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		"%s\n", __func__);

	if (copy_from_user(buf, user_buf, count)) {
		ret = -EFAULT;
		goto out_unlock;
	}
	ret = sscanf(buf, "%x", &debug_level);
	if (ret != 1) {
		ret = -EINVAL;
		goto out_unlock;
	}

    rda5890_dbg_level = debug_level;
	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		"set debug_level = 0x%x\n",rda5890_dbg_level);

	ret = count;
out_unlock:
	free_page(addr);
	return ret;
}

static int debug_read_flag = 0;

static ssize_t rda5890_sdio_read(struct file *file, 
				const char __user *user_buf, size_t count,
				loff_t *ppos)
{
	//struct rda5890_private *priv = file->private_data;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	int ret;

	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		"%s\n", __func__);

	if (copy_from_user(buf, user_buf, count)) {
		ret = -EFAULT;
		goto out_unlock;
	}
	ret = sscanf(buf, "%d", &debug_read_flag);
	if (ret != 1) {
		ret = -EINVAL;
		goto out_unlock;
	}

out_unlock:
	free_page(addr);
	return count;
}

static ssize_t rda5890_sdio_write(struct file *file,
				const char __user *user_buf, size_t count,
				loff_t *ppos)
{
	struct rda5890_private *priv = file->private_data;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	int iter, len, i;
	int ret;

	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		"%s\n", __func__);

	if (copy_from_user(buf, user_buf, count)) {
		ret = -EFAULT;
		goto out_unlock;
	}
	ret = sscanf(buf, "%d %d", &iter, &len);
	if (ret != 2) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (len > 1660) {
		ret = -EINVAL;
		goto out_unlock;
	}

	for (i=0; i<len; i++) {
		buf[i] = len - i - 1;
	}

	for (i=0;i<iter;i++) {
		//RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		//	"Host to Card, len = %d\n", len);

		ret = priv->hw_host_to_card(priv, buf, len, DATA_REQUEST_PACKET);

		//RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		//	"Host to Card done, ret = %d\n", ret);
	}

out_unlock:
	free_page(addr);
	return count;
}

#define SDIO_TEST_CMD_MAGIC        0x55664433
#define SDIO_TEST_CMD_LEN          16

#define SDIO_TEST_CMD_TYPE_H2C_START            1
#define SDIO_TEST_CMD_TYPE_H2C_STOP             2
#define SDIO_TEST_CMD_TYPE_H2C_STATUS           3
#define SDIO_TEST_CMD_TYPE_C2H_START            4
#define SDIO_TEST_CMD_TYPE_C2H_PILOT            5
#define SDIO_TEST_CMD_TYPE_C2H_END              6

static int recv_time_start, recv_time_end;
static int send_time_start, send_time_end;

void rda5890_sdio_test_card_to_host(char *buf, unsigned short len)
{
	int i;
	int cmd, cmd_iter, cmd_len;
	int time_ms;
	static int recv_pattern = 0;
	static int recv_tput_flag = 0;
	static int recv_pkts = 0;
	static int recv_bytes = 0;

	//RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
	//	"SDIO TEST Card to Host, len = %d\n", len);

	if (debug_read_flag) {
		RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
			"DEBUG RX, len = %d\n", len);
		dump_buf(buf, len);
	}

	if ((*(volatile unsigned long *)buf == SDIO_TEST_CMD_MAGIC)
			&& len == SDIO_TEST_CMD_LEN) {
		cmd = (int)(*(volatile unsigned long *)(buf + 4));
		cmd_iter = (int)(*(volatile unsigned long *)(buf + 8));
		cmd_len = (int)(*(volatile unsigned long *)(buf + 12));
		//RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		//		"SDIO TEST CMD: cmd = %d, iter = %d, len = %d\n", 
		//		cmd, cmd_iter, cmd_len);
		switch (cmd) {
		case SDIO_TEST_CMD_TYPE_H2C_STATUS:
			RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
				"H2C STATUS CMD \n");
			time_ms = jiffies_to_msecs(send_time_end - send_time_start);
			RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
				"SDIO H2C STATUS: pkts = %d, bytes = %d, time = %d ms\n",
				cmd_iter, cmd_len, time_ms);
			break;
		case SDIO_TEST_CMD_TYPE_C2H_PILOT:
			//RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
			//	"C2H PILOT CMD \n");
			recv_pattern = 0;
			recv_tput_flag = 1;
			recv_pkts = 0;
			recv_bytes = 0;
			recv_time_start = jiffies;
			break;
		case SDIO_TEST_CMD_TYPE_C2H_END:
			//RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
			//	"C2H END CMD \n");
			recv_time_end = jiffies;
			recv_tput_flag = 0;
			time_ms = jiffies_to_msecs(recv_time_end - recv_time_start);
			RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
				"SDIO C2H STATUS: pkts = %d, bytes = %d, time = %d ms\n",
				recv_pkts, recv_bytes, time_ms);
			break;
		case SDIO_TEST_CMD_TYPE_H2C_START:
		case SDIO_TEST_CMD_TYPE_H2C_STOP:
		case SDIO_TEST_CMD_TYPE_C2H_START:
		default:
			RDA5890_ERRP("SDIO TEST CMD: Invalid cmd %d\n", cmd);
		break;
		}
		return;
	}

	for (i=0;i<len;i++) {
		if (recv_pattern == 0) {
			if (buf[i] != (char)(i)) {
				RDA5890_ERRP("data[%d] error, 0x%02x, should be 0x%02x, len = %d\n", 
					i, buf[i], (char)(i), len);
				break;
			}
		}
		else {
			if (buf[i] != (char)(len - i - 1)) {
				RDA5890_ERRP("data[%d] error, 0x%02x, should be 0x%02x, len = %d\n", 
					i, buf[i], (char)(len - i - 1), len);
				break;
			}
		}
	}

	if (recv_tput_flag)
		recv_pattern = !recv_pattern;

	if (recv_tput_flag && i==len) {
		recv_pkts ++;
		recv_bytes += len;
	}
}

static void sdio_tput_test_read(struct rda5890_private *priv, int iter, int len)
{
	char cmd[SDIO_TEST_CMD_LEN];
	int ret;

	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		"%s, iter = %d, len = %d\n", __func__, iter, len);

	(*(volatile unsigned long *)(cmd + 0)) = SDIO_TEST_CMD_MAGIC;
	(*(volatile unsigned long *)(cmd + 4)) = SDIO_TEST_CMD_TYPE_C2H_START;
	(*(volatile unsigned long *)(cmd + 8)) = iter;
	(*(volatile unsigned long *)(cmd + 12)) = len;
	ret = priv->hw_host_to_card(priv, cmd, len ,SDIO_TEST_CMD_LEN);
	if (ret) {
		RDA5890_ERRP("START cmd send fail, ret = %d\n", ret);
	}
}

static void sdio_tput_test_write(struct rda5890_private *priv, int iter, int len)
{
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf_1 = (char *)addr;
	char *buf_2 = (char *)addr + PAGE_SIZE/2;
	char cmd[SDIO_TEST_CMD_LEN];
	int i;
	int ret;

	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		"%s, iter = %d, len = %d\n", __func__, iter, len);

	for (i=0; i<len; i++) {
		buf_1[i] = len - i - 1;
		buf_2[i] = i;
	}

	(*(volatile unsigned long *)(cmd + 0)) = SDIO_TEST_CMD_MAGIC;
	(*(volatile unsigned long *)(cmd + 4)) = SDIO_TEST_CMD_TYPE_H2C_START;
	(*(volatile unsigned long *)(cmd + 8)) = iter;
	(*(volatile unsigned long *)(cmd + 12)) = len;
	ret = priv->hw_host_to_card(priv, cmd, len,SDIO_TEST_CMD_LEN);
	if (ret) {
		RDA5890_ERRP("START cmd send fail, ret = %d\n", ret);
		goto out;
	}

	send_time_start = jiffies;
	for (i=0;i<iter;i++) {
		if (!(i & 1))
			ret = priv->hw_host_to_card(priv, buf_1, len, DATA_REQUEST_PACKET);
		else
			ret = priv->hw_host_to_card(priv, buf_2, len, DATA_REQUEST_PACKET);
		if (ret) {
			RDA5890_ERRP("packet %d send fail, ret = %d\n", i, ret);
			goto out;
		}
	}
	send_time_end = jiffies;

	(*(volatile unsigned long *)(cmd + 0)) = SDIO_TEST_CMD_MAGIC;
	(*(volatile unsigned long *)(cmd + 4)) = SDIO_TEST_CMD_TYPE_H2C_STOP;
	(*(volatile unsigned long *)(cmd + 8)) = iter;
	(*(volatile unsigned long *)(cmd + 12)) = len;
	ret = priv->hw_host_to_card(priv, cmd, len, SDIO_TEST_CMD_LEN);
	if (ret) {
		RDA5890_ERRP("START cmd send fail, ret = %d\n", ret);
		goto out;
	}

out:
	free_page(addr);
}

static ssize_t rda5890_sdio_tput(struct file *file,
				const char __user *user_buf, size_t count,
				loff_t *ppos)
{
	struct rda5890_private *priv = file->private_data;
	int ret;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	int wr, iter, len;

	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		"%s\n", __func__);

	if (copy_from_user(buf, user_buf, count)) {
		ret = -EFAULT;
		goto out_unlock;
	}
	ret = sscanf(buf, "%d %d %d", &wr, &iter, &len);
	if (ret != 3) {
		RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
			"Error Input, format shall be [wr iter len]\n");
		ret = -EINVAL;
		goto out_unlock;
	}

	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_TRACE,
		"input wr = %d, iter = %d, len = %d\n",
		wr, iter, len);

	if (wr)
		sdio_tput_test_write(priv, iter, len);
	else
		sdio_tput_test_read(priv, iter, len);

	ret = count;
out_unlock:
	free_page(addr);
	return ret;
}

#define FOPS(fread, fwrite) { \
	.owner = THIS_MODULE, \
	.open = open_file_generic, \
	.read = (fread), \
	.write = (fwrite), \
}

struct rda5890_debugfs_files {
	char *name;
	int perm;
	struct file_operations fops;
};

static struct rda5890_debugfs_files debugfs_files[] = {
	{ "debug", 0444, FOPS(rda5890_debug_read, rda5890_debug_write), },
	{ "debugarea", 0444, FOPS(rda5890_debugarea_read, rda5890_debugarea_write), },
	{ "debuglevel", 0444, FOPS(rda5890_debuglevel_read, rda5890_debuglevel_write), },
	{ "sdioread", 0444, FOPS(NULL, rda5890_sdio_read), },
	{ "sdiowrite", 0444, FOPS(NULL, rda5890_sdio_write), },
	{ "sdiotput", 0444, FOPS(NULL, rda5890_sdio_tput), },
};

void rda5890_debugfs_init(void)
{
	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
		"%s\n", __func__);

	if (!rda5890_dir)
		rda5890_dir = debugfs_create_dir("rda5890", NULL);

	return;
}

void rda5890_debugfs_remove(void)
{
	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
		"%s\n", __func__);

	if (rda5890_dir)
		 debugfs_remove(rda5890_dir);

	return;
}

void rda5890_debugfs_init_one(struct rda5890_private *priv)
{
	int i;
	struct rda5890_debugfs_files *files;
	if (!rda5890_dir)
		goto exit;

	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
		"%s\n", __func__);

	priv->debugfs_dir = debugfs_create_dir("rda5890_dev", rda5890_dir);
	if (!priv->debugfs_dir)
		goto exit;

	for (i=0; i<ARRAY_SIZE(debugfs_files); i++) {
		files = &debugfs_files[i];
		priv->debugfs_files[i] = debugfs_create_file(files->name,
							     files->perm,
							     priv->debugfs_dir,
							     priv,
							     &files->fops);
	}

exit:
	return;
}

void rda5890_debugfs_remove_one(struct rda5890_private *priv)
{
	int i;

	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
		"%s\n", __func__);

	for(i=0; i<ARRAY_SIZE(debugfs_files); i++)
		debugfs_remove(priv->debugfs_files[i]);
	debugfs_remove(priv->debugfs_dir);
}


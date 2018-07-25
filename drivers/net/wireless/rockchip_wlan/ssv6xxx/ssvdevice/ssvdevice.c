/*
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 *
 * This program is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or 
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/errno.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif
#include "ssv_cmd.h"
#include "ssv_cfg.h"
#include <linux/fs.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/ctype.h>
MODULE_AUTHOR("iComm Semiconductor Co., Ltd");
MODULE_DESCRIPTION("Shared library for SSV wireless LAN cards.");
MODULE_LICENSE("Dual BSD/GPL");
static char *stacfgpath = NULL;
EXPORT_SYMBOL(stacfgpath);
module_param(stacfgpath, charp, 0000);
MODULE_PARM_DESC(stacfgpath, "Get path of sta cfg");
char *cfgfirmwarepath = NULL;
EXPORT_SYMBOL(cfgfirmwarepath);
module_param(cfgfirmwarepath, charp, 0000);
MODULE_PARM_DESC(cfgfirmwarepath, "Get firmware path");
char* ssv_initmac = NULL;
EXPORT_SYMBOL(ssv_initmac);
module_param(ssv_initmac, charp, 0644);
MODULE_PARM_DESC(ssv_initmac, "Wi-Fi MAC address");
u32 ssv_devicetype = 0;
EXPORT_SYMBOL(ssv_devicetype);
#ifdef CONFIG_DEBUG_FS
static struct dentry *debugfs;
#endif
struct proc_dir_entry *procfs;
static char *ssv6xxx_cmd_buf;
char *ssv6xxx_result_buf;
extern struct ssv6xxx_cfg_cmd_table cfg_cmds[];
extern struct ssv6xxx_cfg ssv_cfg;
char DEFAULT_CFG_PATH[] = "/vendor/etc/firmware/ssv6051-wifi.cfg";
static int ssv6xxx_dbg_open(struct inode *inode, struct file *filp)
{
    filp->private_data = inode->i_private;
    return 0;
}
static ssize_t ssv6xxx_dbg_read(struct file *filp, char __user *buffer,
                size_t count, loff_t *ppos)
{
    int len;
    if (*ppos != 0)
        return 0;
    len = strlen(ssv6xxx_result_buf) + 1;
    if (len == 1)
        return 0;
    if (copy_to_user(buffer, ssv6xxx_result_buf, len))
        return -EFAULT;
    ssv6xxx_result_buf[0] = 0x00;
    return len;
}
static ssize_t ssv6xxx_dbg_write(struct file *filp, const char __user *buffer,
                size_t count, loff_t *ppos)
{
    if (*ppos != 0 || count > 255)
        return 0;
    if (copy_from_user(ssv6xxx_cmd_buf, buffer, count))
        return -EFAULT;
    ssv6xxx_cmd_buf[count-1] = 0x00;
    ssv_cmd_submit(ssv6xxx_cmd_buf);
    return count;
}
size_t read_line(struct file *fp, char *buf, size_t size)
{
 size_t num_read = 0;
 size_t total_read = 0;
 char *buffer;
 char ch;
 size_t start_ignore = 0;
 if (size <= 0 || buf == NULL) {
  total_read = -EINVAL;
  return -EINVAL;
 }
 buffer = buf;
 for (;;) {
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,4,37)
  if (fp->f_op && fp->f_op->read)
   num_read = fp->f_op->read(fp, &ch, 1, &fp->f_pos);
#else
  num_read = vfs_read(fp, &ch, 1, &fp->f_pos);
#endif
  if (num_read < 0) {
   if (num_read == EINTR)
    continue;
   else
    return -1;
  }
  else if (num_read == 0) {
   if (total_read == 0)
    return 0;
   else
    break;
  }
  else {
   if (ch == '#')
    start_ignore = 1;
   if (total_read < size - 1) {
    total_read++;
    if (start_ignore)
     *buffer++ = '\0';
    else
     *buffer++ = ch;
   }
   if (ch == '\n')
    break;
  }
 }
 *buffer = '\0';
 return total_read;
}
int ischar(char *c)
{
 int is_char = 1;
 while(*c) {
  if (isalpha(*c) || isdigit(*c) || *c == '_' || *c == ':' || *c == '/' || *c == '.' || *c == '-')
   c++;
  else {
   is_char = 0;
   break;
  }
 }
 return is_char;
}
void sta_cfg_set(char *stacfgpath)
{
 struct file *fp = (struct file *) NULL;
 char buf[MAX_CHARS_PER_LINE], cfg_cmd[32], cfg_value[32];
 mm_segment_t fs;
 size_t s, read_len = 0, is_cmd_support = 0;
 printk("\n*** %s, %s ***\n\n", __func__, stacfgpath);
    if (stacfgpath == NULL) {
        stacfgpath = DEFAULT_CFG_PATH;
        printk("redirect to %s\n", stacfgpath);
    }
 memset(&ssv_cfg, 0, sizeof(ssv_cfg));
 memset(buf, 0, sizeof(buf));
 fp = filp_open(stacfgpath, O_RDONLY, 0);
 if (IS_ERR(fp) || fp == NULL) {
  printk("ERROR: filp_open\n");
        WARN_ON(1);
  return;
 }
 if (fp->f_path.dentry == NULL) {
  printk("ERROR: dentry NULL\n");
        WARN_ON(1);
  return;
 }
 do {
  memset(cfg_cmd, '\0', sizeof(cfg_cmd));
  memset(cfg_value, '\0', sizeof(cfg_value));
  fs = get_fs();
  set_fs(get_ds());
  read_len = read_line(fp, buf, MAX_CHARS_PER_LINE);
  set_fs(fs);
  sscanf(buf, "%s = %s", cfg_cmd, cfg_value);
  if (!ischar(cfg_cmd) || !ischar(cfg_value)) {
   printk("ERORR invalid parameter: %s\n", buf);
   WARN_ON(1);
   continue;
  }
  is_cmd_support = 0;
  for(s=0; cfg_cmds[s].cfg_cmd != NULL; s++) {
   if (strcmp(cfg_cmds[s].cfg_cmd, cfg_cmd)==0) {
    cfg_cmds[s].translate_func(cfg_value,
     cfg_cmds[s].var, cfg_cmds[s].arg);
    is_cmd_support = 1;
    break;
   }
  }
  if (!is_cmd_support && strlen(cfg_cmd) > 0) {
   printk("ERROR Unsupported command: %s", cfg_cmd);
   WARN_ON(1);
  }
 } while (read_len > 0);
 filp_close(fp, NULL);
}
static struct file_operations ssv6xxx_dbg_fops = {
    .owner = THIS_MODULE,
    .open = ssv6xxx_dbg_open,
    .read = ssv6xxx_dbg_read,
    .write = ssv6xxx_dbg_write,
};
#if (defined(CONFIG_SSV_SUPPORT_ANDROID)||defined(CONFIG_SSV_BUILD_AS_ONE_KO))
extern int ssv6xxx_hci_init(void);
extern void ssv6xxx_hci_exit(void);
extern int ssv6xxx_init(void);
extern void ssv6xxx_exit(void);
extern int ssv6xxx_sdio_init(void);
extern void ssv6xxx_sdio_exit(void);
#endif
#if (defined(CONFIG_SSV_SUPPORT_ANDROID)||defined(CONFIG_SSV_BUILD_AS_ONE_KO))
int ssvdevice_init(void)
#else
static int __init ssvdevice_init(void)
#endif
{
    ssv6xxx_cmd_buf = (char *)kzalloc(CLI_BUFFER_SIZE+CLI_RESULT_BUF_SIZE, GFP_KERNEL);
    if (!ssv6xxx_cmd_buf)
        return -ENOMEM;
    ssv6xxx_result_buf = ssv6xxx_cmd_buf+CLI_BUFFER_SIZE;
    ssv6xxx_cmd_buf[0] = 0x00;
    ssv6xxx_result_buf[0] = 0x00;
#ifdef CONFIG_DEBUG_FS
    debugfs = debugfs_create_dir(DEBUG_DIR_ENTRY,
         NULL);
 if (!debugfs)
  return -ENOMEM;
 debugfs_create_u32(DEBUG_DEVICETYPE_ENTRY, S_IRUGO|S_IWUSR, debugfs, &ssv_devicetype);
    debugfs_create_file(DEBUG_CMD_ENTRY, S_IRUGO|S_IWUSR, debugfs, NULL, &ssv6xxx_dbg_fops);
#endif
 procfs = proc_mkdir(DEBUG_DIR_ENTRY, NULL);
 if (!procfs)
  return -ENOMEM;
    proc_create(DEBUG_CMD_ENTRY, S_IRUGO|S_IWUGO, procfs, &ssv6xxx_dbg_fops);
 sta_cfg_set(stacfgpath);
#if (defined(CONFIG_SSV_SUPPORT_ANDROID)||defined(CONFIG_SSV_BUILD_AS_ONE_KO))
    {
        int ret;
        ret = ssv6xxx_hci_init();
        if(!ret){
            ret = ssv6xxx_init();
        }if(!ret){
            ret = ssv6xxx_sdio_init();
        }
        return ret;
    }
#endif
    return 0;
}
#if (defined(CONFIG_SSV_SUPPORT_ANDROID)||defined(CONFIG_SSV_BUILD_AS_ONE_KO))
void ssvdevice_exit(void)
#else
static void __exit ssvdevice_exit(void)
#endif
{
#if (defined(CONFIG_SSV_SUPPORT_ANDROID)||defined(CONFIG_SSV_BUILD_AS_ONE_KO))
    ssv6xxx_exit();
    ssv6xxx_hci_exit();
    ssv6xxx_sdio_exit();
#endif
#ifdef CONFIG_DEBUG_FS
    debugfs_remove_recursive(debugfs);
#endif
 remove_proc_entry(DEBUG_CMD_ENTRY, procfs);
 remove_proc_entry(DEBUG_DIR_ENTRY, NULL);
    kfree(ssv6xxx_cmd_buf);
}
#if (defined(CONFIG_SSV_SUPPORT_ANDROID)||defined(CONFIG_SSV_BUILD_AS_ONE_KO))
EXPORT_SYMBOL(ssvdevice_init);
EXPORT_SYMBOL(ssvdevice_exit);
#else
module_init(ssvdevice_init);
module_exit(ssvdevice_exit);
module_param_named(devicetype,ssv_devicetype, uint , S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(devicetype, "Enable sdio bridge Mode/Wifi Mode.");
#endif

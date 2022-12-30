#include "ecrnx_debugfs_custom.h"
#include "ecrnx_debugfs_func.h"
#include "slave_log_buf.h"
#include <linux/file.h>

#ifdef CONFIG_ECRNX_DEBUGFS
#define DEFINE_SHOW_ATTRIBUTE(__name)                    \
static int __name ## _open(struct inode *inode, struct file *file)    \
{                                    \
    return single_open(file, __name ## _show, inode->i_private);    \
}                                    \
                                    \
static const struct file_operations __name ## _fops = {            \
    .owner        = THIS_MODULE,                  \
    .open        = __name ## _open,               \
    .read        = seq_read,                      \
    .llseek        = seq_lseek,                   \
    .release    = single_release,                 \
}

#define DEFINE_SHOW_ATTRIBUTE_EX(__name)                    \
static int __name ## _open(struct inode *inode, struct file *file)    \
{                                    \
    return single_open(file, __name ## _show, inode->i_private);    \
}                                    \
                                    \
static const struct file_operations __name ## _fops = {            \
    .owner        = THIS_MODULE,                  \
    .open        = __name ## _open,               \
    .read        = seq_read,                      \
    .write        = __name ## _write,             \
    .llseek        = seq_lseek,                   \
    .release    = single_release,                 \
}

static long custom_sys_write(unsigned int fd, const char __user *buf, size_t count)
{
   long ret = -EBADF;
   struct file *file = NULL;
   mm_segment_t status;
   loff_t offset;

   if (!buf) {
       printk("Write buffer was empty.\n");
       return ret;
   }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
   file = fget(fd);
   if (file) {
       offset = file->f_pos;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
        ret = kernel_write(file, buf, count, (loff_t *)&offset);
#else
        ret = kernel_write(file, buf, count, 0);
#endif
       file->f_pos = offset;
       fput(file);
   }
#else
   print_hex_dump(KERN_DEBUG, DBG_PREFIX, DUMP_PREFIX_NONE, 16, 1, buf, count, false);
#endif
   return ret;
}


void seq_reg_display_u32(struct seq_file *seq, u32 addr, u32 *reg, u32 len)
{
    u32 i;

    for (i=0; i<len; i++)
    {
        if (i%4 == 0)
            seq_printf(seq, "0x%08X  ", (addr+ 4*i));

        seq_printf(seq, " 0x%08X", reg[i]);

        if (i%4 == 3)
            seq_printf(seq, "\n");
    }

    if (len % 4)
        seq_printf(seq, "\n");
}

void seq_reg_display_u8(struct seq_file *seq, u32 addr, u8 *reg, u32 len)
{
    u32 i;

    for (i=0; i<len; i++)
    {
        if (i%16 == 0)
            seq_printf(seq, "0x%04X  ", (addr+i)&0xFFFF);

        seq_printf(seq, " %02X", reg[i]);

        if (i%16 == 7)
            seq_printf(seq, "  ");

        if (i%16 == 15)
            seq_printf(seq, "\n");
    }

    if (len % 16)
        seq_printf(seq, "\n");
}
void read_reg_display_u32(u32 addr, u32 *reg, u32 len)
{
    u32 i;
    u32 size = len*11 + (len + 3)/4*16 + len/4;
    u32 point = 0;
    char * buf = NULL;

    buf = kmalloc(size, GFP_ATOMIC);
    if(buf == NULL)
    {
        return;
    }
    for (i=0; i<len; i++)
    {
        if (i%4 == 0)
        {
            sprintf(buf+point,"addr:0x%08X", (addr+ 4*i));
            point += 15;
            buf[point] = '\n';
            point += 1;

        }

        sprintf(buf+point,"0x%08X", reg[i]);
        point += 10;
        buf[point] = '\n';
        point += 1;


        if (i%4 == 3)
        {
            buf[point] = '\n';
            point += 1;
        }
    }
    custom_sys_write(0,buf,size);
    if(buf != NULL)
    {
        kfree(buf);
        buf = NULL;
    }

}
/*
void reg_display_u32(u32 addr, u32 *reg, u32 len)
{
    u32 i;

    for (i=0; i<len; i++)
    {
        if (i%4 == 0)
            ECRNX_PRINT("0x%08X", (addr+ 4*i));

        ECRNX_PRINT("0x%08X", reg[i]);

        if (i%4 == 3)
            printk("\n");
    }

    if (len % 4)
        printk("\n");
}
*/
void reg_display_u8(u32 addr, u8 *reg, u32 len)
{
    u32 i;

    for (i=0; i<len; i++)
    {
        if (i%16 == 0)
            ECRNX_PRINT("0x%04X  ", (addr+i)&0xFFFF);

        ECRNX_PRINT(" %02X", reg[i]);

        if (i%16 == 7)
            printk("  ");

        if (i%16 == 15)
            printk("\n");
    }

    if (len % 16)
        printk("\n");
}

static int drv_cfg_show(struct seq_file *seq, void *v)
{
//#define DRV_CFG_DETAILS

    u8 host_ver[32];

    ecrnx_host_ver_get(host_ver);

    seq_printf(seq, "Kernel Version : %d.%d.%d\n", LINUX_VERSION_CODE >> 16, (LINUX_VERSION_CODE >> 8)&0xFF, LINUX_VERSION_CODE&0xFF);
    seq_printf(seq, "Driver Version : %s\n", host_ver);
    seq_printf(seq, "------------------------------------------------\n");

#ifdef CONFIG_ECRNX_ESWIN
    seq_printf(seq, "CONFIG_ECRNX_ESWIN=1\n");
#else
    #ifdef DRV_CFG_DETAILS
        seq_printf(seq, "CONFIG_ECRNX_ESWIN=0\n");
    #endif
#endif


#ifdef CONFIG_ECRNX_FULLMAC
    seq_printf(seq, "CONFIG_ECRNX_FULLMAC=1\n");
#else
    #ifdef DRV_CFG_DETAILS
        seq_printf(seq, "CONFIG_ECRNX_FULLMAC=0\n");
    #endif
#endif


#ifdef CONFIG_ECRNX_SOFTMAC
    seq_printf(seq, "CONFIG_ECRNX_SOFTMAC=1\n");
#else
    #ifdef DRV_CFG_DETAILS
        seq_printf(seq, "CONFIG_ECRNX_SOFTMAC=0\n");
    #endif
#endif


#ifdef CONFIG_ECRNX_ESWIN_USB
    seq_printf(seq, "CONFIG_ECRNX_ESWIN_USB=1\n");
#else
    #ifdef DRV_CFG_DETAILS
        seq_printf(seq, "CONFIG_ECRNX_ESWIN_USB=0\n");
    #endif
#endif


#ifdef CONFIG_ECRNX_ESWIN_SDIO
    seq_printf(seq, "CONFIG_ECRNX_ESWIN_SDIO=1\n");
#else
    #ifdef DRV_CFG_DETAILS
        seq_printf(seq, "CONFIG_ECRNX_ESWIN_SDIO=0\n");
    #endif
#endif


#ifdef CONFIG_ECRNX_HE
    seq_printf(seq, "CONFIG_ECRNX_HE=1\n");
#else
    #ifdef DRV_CFG_DETAILS
        seq_printf(seq, "CONFIG_ECRNX_HE=0\n");
    #endif
#endif


#ifdef CONFIG_ECRNX_P2P
    seq_printf(seq, "CONFIG_ECRNX_P2P=1\n");
#else
    #ifdef DRV_CFG_DETAILS
        seq_printf(seq, "CONFIG_ECRNX_P2P=0\n");
    #endif
#endif


#ifdef CONFIG_ECRNX_FHOST
        seq_printf(seq, "CONFIG_ECRNX_FHOST=1\n");
#else
    #ifdef DRV_CFG_DETAILS
            seq_printf(seq, "CONFIG_ECRNX_FHOST=0\n");
    #endif
#endif


#ifdef CONFIG_ECRNX_SDM
        seq_printf(seq, "CONFIG_ECRNX_SDM=1\n");
#else
    #ifdef DRV_CFG_DETAILS
            seq_printf(seq, "CONFIG_ECRNX_SDM=0\n");
    #endif
#endif


#ifdef CONFIG_ECRNX_TL4
        seq_printf(seq, "CONFIG_ECRNX_TL4=1\n");
#else
    #ifdef DRV_CFG_DETAILS
            seq_printf(seq, "CONFIG_ECRNX_TL4=0\n");
    #endif
#endif


#ifdef CONFIG_ECRNX_MUMIMO_TX
        seq_printf(seq, "CONFIG_ECRNX_MUMIMO_TX=1\n");
#else
    #ifdef DRV_CFG_DETAILS
            seq_printf(seq, "CONFIG_ECRNX_MUMIMO_TX=0\n");
    #endif
#endif


#ifdef CONFIG_ECRNX_RADAR
        seq_printf(seq, "CONFIG_ECRNX_RADAR=1\n");
#else
    #ifdef DRV_CFG_DETAILS
            seq_printf(seq, "CONFIG_ECRNX_RADAR=0\n");
    #endif
#endif


#ifdef CONFIG_ECRNX_BCMC
        seq_printf(seq, "CONFIG_ECRNX_BCMC=1\n");
#else
    #ifdef DRV_CFG_DETAILS
            seq_printf(seq, "CONFIG_ECRNX_BCMC=0\n");
    #endif
#endif


#ifdef CONFIG_ECRNX_MON_DATA
        seq_printf(seq, "CONFIG_ECRNX_MON_DATA=1\n");
#else
    #ifdef DRV_CFG_DETAILS
            seq_printf(seq, "CONFIG_ECRNX_MON_DATA=0\n");
    #endif
#endif


#ifdef CONFIG_ECRNX_SW_PROFILING
        seq_printf(seq, "CONFIG_ECRNX_SW_PROFILING=1\n");
#else
    #ifdef DRV_CFG_DETAILS
            seq_printf(seq, "CONFIG_ECRNX_SW_PROFILING=0\n");
    #endif
#endif


#ifdef CONFIG_ECRNX_DBG
        seq_printf(seq, "CONFIG_ECRNX_DBG=1\n");
#else
    #ifdef DRV_CFG_DETAILS
            seq_printf(seq, "CONFIG_ECRNX_DBG=0\n");
    #endif
#endif


#ifdef CFG_WOW_SUPPORT
            seq_printf(seq, "CFG_WOW_SUPPORT=1\n");
#else
    #ifdef DRV_CFG_DETAILS
                seq_printf(seq, "CFG_WOW_SUPPORT=0\n");
    #endif
#endif

    return 0;
}
DEFINE_SHOW_ATTRIBUTE(drv_cfg);


static int log_level_show(struct seq_file *seq, void *v)
{
    LOG_CTL_ST log;

    if (0 == ecrnx_log_level_get(&log))
        seq_printf(seq, "log_level:%d  dir:%d\n", log.level, log.dir);
    else
        seq_printf(seq, "\n");

    return 0;
}

static ssize_t log_level_write(struct file *filp, const char __user *buffer,
                            size_t count, loff_t *ppos)
{
    u8 kbuf[128]={0};
    u32 level,dir;

    if (count > 6)  // 255:0\n
        return -1;

    if (0 != copy_from_user(kbuf, buffer, count))
        return -1;
    sscanf(kbuf, "%d:%d\n", &level, &dir);

    if (dir > 1)
        return -1;

    ecrnx_fw_log_level_set(level, dir);

    return count;
}
DEFINE_SHOW_ATTRIBUTE_EX(log_level);

static ssize_t mac_log_read(struct file *file , char __user *user_buf,
                                   size_t count, loff_t *ppos)
{
    struct ring_buffer *buf_handle;
    int len = 0;
    char buf[512] = {0};
    if (false == ecrnx_log_host_enable())
        return 0;
#ifdef CONFIG_ECRNX_ESWIN_USB
    buf_handle = usb_dbg_buf_get();
    ring_buffer_scrolling_display(buf_handle,true);
    while(buf_handle->show)
    {
        len = ring_buffer_len(buf_handle);
        len = min(len, 512);
        //ECRNX_PRINT("mac_log_read len:%d %d %d", len,buf_handle->write_point, buf_handle->read_point);
        if(len > 0)
        {
            ring_buffer_get(buf_handle,buf,len);
            custom_sys_write(0,buf,len);
            msleep(35);
        }
        else
        {
            msleep(500);
        }
        cond_resched();
    }
#endif

    return count;
}

static ssize_t mac_log_write(struct file *filp, const char __user *buffer,
                            size_t count, loff_t *ppos)
{
    u8 kbuf[128]={0};
    u32 show;
    struct ring_buffer *buf_handle;

    if (count > 2)  // 255:0\n
        return -1;

    if (0 != copy_from_user(kbuf, buffer, count))
        return -1;
    sscanf(kbuf, "%d:%d\n", &show);
#ifdef CONFIG_ECRNX_ESWIN_USB
    if (show == 0)
    {
        buf_handle = usb_dbg_buf_get();
        ring_buffer_scrolling_display(buf_handle, false);
    }
#endif

    return count;
}

static const struct file_operations new_mac_log_fops = { \
    .read   = mac_log_read,                         \
    .write  = mac_log_write,                         \
};

static int ver_info_show(struct seq_file *seq, void *v)
{
    u8 host_ver[32];
    u8 build_time[32];

    ecrnx_host_ver_get(host_ver);
    ecrnx_build_time_get(build_time);

    seq_printf(seq, "%s\n", host_ver);
    seq_printf(seq, "build time: %s\n", build_time);

    return 0;
}
DEFINE_SHOW_ATTRIBUTE(ver_info);

extern bin_head_data head;
static int fw_info_show(struct seq_file *seq, void *v)
{
    struct tm timenow = {0};

    localtime(&timenow, head.UTC_time);
    if(head.fw_Info != NULL)
    {
        seq_printf(seq, "sdk_verson:%s\n", head.fw_Info);
    }
    seq_printf(seq, "fw_crc:0x%8x\n", head.crc32);
    seq_printf(seq, "fw_build_time: %04d-%02d-%02d %02d:%02d:%02d\n",(int)timenow.tm_year,timenow.tm_mon,timenow.tm_mday,timenow.tm_hour,timenow.tm_min,timenow.tm_sec);
    return 0;
}

DEFINE_SHOW_ATTRIBUTE(fw_info);

struct dentry *ecr6600u_dir = NULL;
struct dentry *drv_cfg, *log_level, *mac_log, *ver_info, *fw_info;

int ecrnx_debugfs_info_init(void      *private_data)
{
    ecr6600u_dir = debugfs_create_dir("ecr6600u", NULL);
    if (!ecr6600u_dir)
        goto Fail;

    drv_cfg = debugfs_create_file("drv_cfg", 0444, ecr6600u_dir, private_data, &drv_cfg_fops);
    if (!drv_cfg)
        goto Fail;

    log_level = debugfs_create_file("log_level", 0666, ecr6600u_dir, private_data, &log_level_fops);
    if (!log_level)
        goto Fail;

    mac_log = debugfs_create_file("maclog", 0444, ecr6600u_dir, private_data, &new_mac_log_fops);
    if (!mac_log)
        goto Fail;

    ver_info = debugfs_create_file("ver_info", 0444, ecr6600u_dir, private_data, &ver_info_fops);
    if (!ver_info)
        goto Fail;

    fw_info = debugfs_create_file("fw_info", 0444, ecr6600u_dir, private_data, &fw_info_fops);
    if (!fw_info)
        goto Fail;
    return 0;

Fail:
    return -ENOENT;
}


static int rf_info_show(struct seq_file *seq, void *v)
{
    RF_INFO_ST cur, oper;

    if (0 == ecrnx_rf_info_get(seq, IF_STA, &cur, &oper))
    {
        seq_printf(seq, "cur_ch=%d, cur_bw=%d, cur_ch_offset=%d\n", cur.ch, cur.bw, cur.ch_offset);
        // seq_printf(seq, "oper_ch=%d, oper_bw=%d, oper_ch_offset=%d\n", oper.ch, oper.bw, oper.ch_offset);
    }

    return 0;
}
DEFINE_SHOW_ATTRIBUTE(rf_info);

static int mac_reg_dump_show(struct seq_file *seq, void *v)
{
    ecrnx_mac_reg_dump(seq);

    return 0;
}
DEFINE_SHOW_ATTRIBUTE(mac_reg_dump);

static int rf_reg_dump_show(struct seq_file *seq, void *v)
{
    ecrnx_rf_reg_dump(seq);

    return 0;
}
DEFINE_SHOW_ATTRIBUTE(rf_reg_dump);

static int bb_reg_dump_show(struct seq_file *seq, void *v)
{
    ecrnx_bb_reg_dump(seq);

    return 0;
}
DEFINE_SHOW_ATTRIBUTE(bb_reg_dump);

static int country_code_show(struct seq_file *seq, void *v)
{
    char country_code[3];
    if (0 == ecrnx_country_code_get(seq, country_code))
    {
        seq_printf(seq, "%s\n", country_code);
    }
    else
    {
        seq_printf(seq, "UNSPECIFIED\n");
    }

    return 0;
}
DEFINE_SHOW_ATTRIBUTE(country_code);

static int mac_addr_show(struct seq_file *seq, void *v)
{
    u8 mac_addr_info[128];

    if (0 == ecrnx_mac_addr_get_ex(seq, mac_addr_info))
    {
        seq_printf(seq, "%s", mac_addr_info);
    }
    else
    {
        seq_printf(seq, "--\n");
    }

    return 0;
}
DEFINE_SHOW_ATTRIBUTE(mac_addr);

static int efuse_map_show(struct seq_file *seq, void *v)
{
    ecrnx_efuse_map_dump(seq);

    return 0;
}
DEFINE_SHOW_ATTRIBUTE(efuse_map);

static int read_reg_show(struct seq_file *seq, void *v)
{
    return 0;
}
static ssize_t read_reg_write(struct file *filp, const char __user *buffer,
                            size_t count, loff_t *ppos)
{
    u8 kbuf[128]={0};
    u32 addr, len;
    int ret_code;
    char * read_reg_buf = NULL;

    if (count > sizeof(kbuf))
        return -1;

    if (0 != copy_from_user(kbuf, buffer, count))
        return -1;

    ret_code = sscanf(kbuf, "0x%x %d\n", &addr, &len);
    if (ret_code != 2)
    {
        ECRNX_ERR("Parameter error.\n");
        return -1;
    }

    if ( addr%4 )
    {
        ECRNX_ERR("Address is invalid.\n");
        return -1;
    }

    if (0 == ecrnx_slave_reg_read(addr, reg_buf, len * sizeof(reg_buf[0])))
        read_reg_display_u32(addr, reg_buf, len);
    return count;
}
DEFINE_SHOW_ATTRIBUTE_EX(read_reg);
static int write_reg_show(struct seq_file *seq, void *v)
{
    return 0;
}

static ssize_t write_reg_write(struct file *filp, const char __user *buffer,
                            size_t count, loff_t *ppos)
{
    u8 kbuf[128]={0};
    u32 addr, data;
    int ret_code,ret = count;
    char* write_reg_result = NULL;

    if (count > sizeof(kbuf))
        ret = -1;

    if (0 != copy_from_user(kbuf, buffer, count))
        ret = -1;

    ret_code = sscanf(kbuf, "0x%x 0x%x\n", &addr, &data);
    if (ret_code != 2)
    {
        write_reg_result = "Parameter error.\n";
        ret = -1;
    }

    if ( addr%4 )
    {
        write_reg_result = "Address is invalid.\n";
        ret = -1;
    }

    if (0 != ecrnx_slave_reg_write(addr, data, 1))
    {
        write_reg_result = "Write error.\n";
        ret = -1;
    }
    else
        write_reg_result = "Write success.\n";
    if(write_reg_result != NULL)
    {
        custom_sys_write(0,write_reg_result,strlen(write_reg_result));
    }
    return ret;
}
DEFINE_SHOW_ATTRIBUTE_EX(write_reg);

static int rf_tx_rx_info_show(struct seq_file *seq, void *v)
{
//    u8 *resp = NULL;
//    u8 cli[]="rf_get_rssi 0 1 2";

//    ecrnx_slave_cli_send(cli, resp);

    return 0;
}

static ssize_t rf_tx_rx_info_write(struct file *filp, const char __user *buffer,
                            size_t count, loff_t *ppos)
{
    u8 kbuf[128]={0};

    if (count > sizeof(kbuf))
        return -1;

    if (0 != copy_from_user(kbuf, buffer, count))
        return -1;

    cli_cmd_parse(kbuf);

    return count;
}

DEFINE_SHOW_ATTRIBUTE_EX(rf_tx_rx_info);

struct dentry *hw_dir = NULL;
struct dentry *hw_rf_info, *hw_mac_reg_dump, *hw_rf_reg_dump, *hw_bb_reg_dump, *hw_country_code, *hw_mac_addr;
struct dentry *hw_efuse_map, *hw_read_reg, *hw_write_reg, *hw_rf_tx_rx_info;

int ecrnx_debugfs_hw_init(void *private_data)
{
    if (!ecr6600u_dir)
        return -ENOENT;

    hw_dir = debugfs_create_dir("hw", ecr6600u_dir);
    if (!hw_dir)
        return -ENOENT;

    hw_rf_info = debugfs_create_file("rf_info", 0444, hw_dir, private_data, &rf_info_fops);
    if (!hw_rf_info)
        goto Fail;

    hw_mac_reg_dump = debugfs_create_file("mac_reg_dump", 0444, hw_dir, private_data, &mac_reg_dump_fops);
    if (!hw_mac_reg_dump)
        goto Fail;

    hw_rf_reg_dump = debugfs_create_file("rf_reg_dump", 0444, hw_dir, private_data, &rf_reg_dump_fops);
    if (!hw_rf_reg_dump)
        goto Fail;

    hw_bb_reg_dump = debugfs_create_file("bb_reg_dump", 0444, hw_dir, private_data, &bb_reg_dump_fops);
    if (!hw_bb_reg_dump)
        goto Fail;

    hw_country_code = debugfs_create_file("country_code", 0444, hw_dir, private_data, &country_code_fops);
    if (!hw_country_code)
        goto Fail;

    hw_mac_addr = debugfs_create_file("mac_addr", 0444, hw_dir, private_data, &mac_addr_fops);
    if (!hw_mac_addr)
        goto Fail;

    hw_efuse_map = debugfs_create_file("efuse_map", 0444, hw_dir, private_data, &efuse_map_fops);
    if (!hw_efuse_map)
        goto Fail;

    hw_read_reg = debugfs_create_file("read_reg", 0222, hw_dir, private_data, &read_reg_fops);
    if (!hw_read_reg)
        goto Fail;

    hw_write_reg = debugfs_create_file("write_reg", 0222, hw_dir, private_data, &write_reg_fops);
    if (!hw_write_reg)
        goto Fail;

    hw_rf_tx_rx_info = debugfs_create_file("rf_tx_rx_info", 0666, hw_dir, private_data, &rf_tx_rx_info_fops);
    if (!hw_rf_tx_rx_info)
        goto Fail;

    return 0;

Fail:
    debugfs_remove_recursive(hw_dir);
    hw_dir = NULL;
    return -ENOENT;
}

#ifndef MAC_FMT
#define MAC_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#endif
#ifndef MAC_ARG
#define MAC_ARG(x) ((u8 *)(x))[0], ((u8 *)(x))[1], ((u8 *)(x))[2], ((u8 *)(x))[3], ((u8 *)(x))[4], ((u8 *)(x))[5]
#endif

static int survey_info_show(struct seq_file *seq, void *v)
{
  int index = 0;
  struct ecrnx_hw *ecrnx_hw;
  struct ecrnx_debugfs_survey_info_tbl *entry;

  if (seq->private != NULL)
      ecrnx_hw = seq->private;
  else
      return -1;
  
//seq_printf(seq, "index  bssid  ch  RSSI  SdBm  Noise    age          flag          ssid \n");
  seq_printf(seq, "%5s  %-17s  %3s  %-3s  %-4s  %-4s  %5s  %32s  %32s\n", "index", "bssid", "ch", "RSSI", "SdBm", "Noise", "age", "flag", "ssid");
  
  list_for_each_entry(entry, &ecrnx_hw->debugfs_survey_info_tbl_ptr, list) {
      seq_printf(seq, "%5d  "MAC_FMT"  %3d  %3d  %4d  %4d  %5d    %32s    %32s\n",
          ++index,
          MAC_ARG(entry->bssid),
          entry->ch,
          entry->rssi,
          entry->sdbm,
          entry->noise,
          entry->age,
          entry->flag,
          entry->ssid);
  }

    return 0;
}
DEFINE_SHOW_ATTRIBUTE(survey_info);

static int sta_info_show(struct seq_file *seq, void *v)
{
    s8 noise_dbm;
    struct cfg80211_chan_def chandef;
    u32 bw_array[]={20, 20, 40, 80, 80, 160, 5, 10};

    if (0 == ecrnx_signal_level_get(seq, IF_STA, &noise_dbm))
        seq_printf(seq, "signal level : %d dBm\n", noise_dbm);
    else
        seq_printf(seq, "signal level : \n");

    if (0 == ecrnx_channel_get(seq, IF_STA, &chandef))
    {
        seq_printf(seq, "frequency    : %d MHz\n", chandef.center_freq1);
        seq_printf(seq, "bandwidth    : %d MHz\n", bw_array[chandef.width]);
    }
    else
    {
        seq_printf(seq, "frequency    : \n");
        seq_printf(seq, "bandwidth    : \n");
    }

    return 0;
}
DEFINE_SHOW_ATTRIBUTE(sta_info);

static int ht_info_show(struct seq_file *seq, void *v)
{
    struct cfg80211_chan_def chandef;
    u32 bw_array[]={20, 20, 40, 80, 80, 160, 5, 10};

    if (0 == ecrnx_channel_get(seq, IF_STA, &chandef))
        seq_printf(seq, "bandwidth    : %d MHz\n", bw_array[chandef.width]);
    else
        seq_printf(seq, "bandwidth    : \n");

    return 0;
}
DEFINE_SHOW_ATTRIBUTE(ht_info);


struct dentry *wlan0_dir = NULL;
struct dentry *wlan0_survey_info, *wlan0_sta_info, *wlan0_ht_info;;

int ecrnx_debugfs_wlan0_init(void *private_data)
{
    if (!ecr6600u_dir)
        return -ENOENT;

    wlan0_dir = debugfs_create_dir("wlan0", ecr6600u_dir);
    if (!wlan0_dir)
        return -ENOENT;

    wlan0_survey_info = debugfs_create_file("survey_info", 0444, wlan0_dir, private_data, &survey_info_fops);
    if (!wlan0_survey_info)
        goto Fail;

    wlan0_sta_info = debugfs_create_file("sta_info", 0444, wlan0_dir, private_data, &sta_info_fops);
    if (!wlan0_sta_info)
        goto Fail;

    wlan0_ht_info = debugfs_create_file("ht_info", 0444, wlan0_dir, private_data, &ht_info_fops);
    if (!wlan0_ht_info)
        goto Fail;

    return 0;

Fail:
    debugfs_remove_recursive(wlan0_dir);
    wlan0_dir = NULL;
    return -ENOENT;
}

static int ap_info_show(struct seq_file *seq, void *v)
{
    struct cfg80211_chan_def chandef;
    u32 bw_array[]={20, 20, 40, 80, 80, 160, 5, 10};
    u8 ssid[IEEE80211_MAX_SSID_LEN];
    u8 sta_mac[NX_REMOTE_STA_MAX][ETH_ALEN+1];
    u32 i;

    seq_printf(seq, "AP Vendor ESWIN\n");

    seq_printf(seq, "ap info\n");
    if (0 == ecrnx_ssid_get(seq, IF_AP, ssid))
        seq_printf(seq, "ssid         : %s\n", ssid);
    else
        seq_printf(seq, "ssid         : \n");

    if (0 == ecrnx_channel_get(seq, IF_AP, &chandef))
    {
        seq_printf(seq, "cur_channel=%d, cur_bwmode=%d(%dMHz), cur_ch_offset=0\n",
            (chandef.center_freq1 - 2412)/5 + 1, chandef.width, bw_array[chandef.width]);
    }
    else
    {
        seq_printf(seq, "cur_channel=, cur_bwmode=(MHz), cur_ch_offset=\n");
    }
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
    if ((chandef.width <= NL80211_CHAN_WIDTH_10) && (chandef.width >= NL80211_CHAN_WIDTH_20_NOHT))
    {
        if (NL80211_CHAN_WIDTH_20_NOHT == chandef.width)
        {
            seq_printf(seq, "ht_en=0\n");
        }
        else
        {
            seq_printf(seq, "ht_en=1\n");
        }
    }
    else
    {
        seq_printf(seq, "ht_en=0\n");
    }
#endif

    seq_printf(seq, "wireless_mode=0xb(B/G/N), rtsen=0, cts2slef=0\n");
    seq_printf(seq, "state=0x10, aid=0, macid=32, raid=0\n");
    seq_printf(seq, "qos_en=1, init_rate=0\n");
    seq_printf(seq, "bwmode=0, ch_offset=0, sgi_20m=1,sgi_40m=1\n");
    seq_printf(seq, "ampdu_enable = 1\n");
    seq_printf(seq, "agg_enable_bitmap=0, candidate_tid_bitmap=0\n");
    seq_printf(seq, "ldpc_cap=0x0, stbc_cap=0x0, beamform_cap=0x0\n");


    seq_printf(seq, "\n");
    seq_printf(seq, "station info\n");
    memset(sta_mac, 0x00, sizeof(sta_mac));
    if (0 == ecrnx_sta_mac_get(seq, IF_AP, sta_mac))
    {
        for (i=0; i<NX_REMOTE_STA_MAX; i++)
        {
            if (sta_mac[i][0])
                seq_printf(seq, "sta's macaddr:%02X:%02X:%02X:%02X:%02X:%02X\n", sta_mac[i][1], sta_mac[i][2],
                                sta_mac[i][3], sta_mac[i][4], sta_mac[i][5], sta_mac[i][6]);
        }
    }

    return 0;
}
DEFINE_SHOW_ATTRIBUTE(ap_info);

static int sta_info_in_ap_show(struct seq_file *seq, void *v)
{
	struct ecrnx_debugfs_sta *sta;
	
	if (seq->private == NULL)
		ECRNX_ERR("error sta_info_in_ap_show\n");

	sta = seq->private;
	
	seq_printf(seq, "cur_channel=%d, cur_bwmode=%d\n", sta->ch_idx, sta->width);
	//seq_printf(m, "wireless_mode=0x%x(%s), rtsen=%d, cts2slef=%d\n", psta->wireless_mode, wl_mode, psta->rtsen, psta->cts2self);
	seq_printf(seq, "aid=%d\n", sta->aid);

	seq_printf(seq, "qos_en=%d, ht_en=%d\n", sta->qos, sta->ht);
	seq_printf(seq, "sgi_20m=%d,sgi_40m=%d\n", sta->sgi_20m, sta->sgi_40m);
	seq_printf(seq, "ampdu_enable = %d\n", sta->ampdu_enable);
	seq_printf(seq, "agg_enable_bitmap=%x, candidate_tid_bitmap=%x\n", sta->agg_enable_bitmap, sta->candidate_tid_bitmap);
	seq_printf(seq, "ldpc_cap=0x%x, stbc_cap=0x%x, beamform_cap=0x%x\n", sta->ldpc_cap, sta->stbc_cap, sta->beamform_cap);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(sta_info_in_ap);

struct dentry *ap0_dir = NULL;
struct dentry *ap0_ap_info;
struct dentry *stas[NX_REMOTE_STA_MAX];

int ecrnx_debugfs_ap0_init(void *private_data)
{
    if (!ecr6600u_dir)
        return -ENOENT;

    ap0_dir = debugfs_create_dir("ap0", ecr6600u_dir);
    if (!ap0_dir)
        return -ENOENT;

    ap0_ap_info = debugfs_create_file("ap_info", 0444, ap0_dir, private_data, &ap_info_fops);
    if (!ap0_ap_info)
        goto Fail;

    return 0;

Fail:
    debugfs_remove_recursive(ap0_dir);
    ap0_dir = NULL;
    return -ENOENT;
}

void ecrnx_debugfs_sta_in_ap_init(void *private_data)
{
	int index;
	char file_name[19];
	struct ecrnx_debugfs_sta *sta;

	sta = private_data;
	index = sta->sta_idx;

	sprintf(file_name, "%02x-%02x-%02x-%02x-%02x-%02x", MAC_ARG(sta->mac_addr));
	stas[index] = debugfs_create_file(file_name, 0444, ap0_dir, private_data, &sta_info_in_ap_fops);
	ECRNX_PRINT("%s:file_name: %s, sta idx:%d,stas:0x%p \n", __func__, file_name, sta->sta_idx, stas[index]);
}

void ecrnx_debugfs_sta_in_ap_del(u8 sta_idx)
{
    struct dentry *dentry_sta;
    struct ecrnx_debugfs_sta *debugfs_sta;

    ECRNX_PRINT("%s: sta_idx:%d, stas:0x%p \n", __func__, sta_idx, stas[sta_idx]);
    dentry_sta = stas[sta_idx];
    if(!dentry_sta)
    {
        ECRNX_ERR("error sta_idx!!!\n");
        return;
    }

    if (dentry_sta->d_inode)
    {
        debugfs_sta = dentry_sta->d_inode->i_private;
        ECRNX_DBG("%s: dentry_sta->d_inode:0x%p, debugfs_sta:0x%p \n", __func__, dentry_sta->d_inode, debugfs_sta);
        if (debugfs_sta)
        {
            debugfs_remove(dentry_sta);
            kfree(debugfs_sta);
        }
    }

    stas[sta_idx] = NULL;
}

static int p2p0_survey_info_show(struct seq_file *seq, void *v)
{
	survey_info_show(seq, v);

    return 0;
}
DEFINE_SHOW_ATTRIBUTE(p2p0_survey_info);

static int p2p_info_show(struct seq_file *seq, void *v)
{
    s8 noise_dbm;
    struct cfg80211_chan_def chandef;
    u32 bw_array[]={20, 20, 40, 80, 80, 160, 5, 10};

    if (0 == ecrnx_signal_level_get(seq, IF_P2P, &noise_dbm))
        seq_printf(seq, "signal level : %d dBm\n", noise_dbm);
    else
        seq_printf(seq, "signal level : \n");

    if (0 == ecrnx_channel_get(seq, IF_P2P, &chandef))
    {
        seq_printf(seq, "frequency    : %d MHz\n", chandef.center_freq1);
        seq_printf(seq, "bandwidth    : %d MHz\n", bw_array[chandef.width]);
    }
    else
    {
        seq_printf(seq, "frequency    : \n");
        seq_printf(seq, "bandwidth    : \n");
    }

    if (NL80211_IFTYPE_P2P_CLIENT == ecrnx_p2p_role_get(seq, IF_P2P))
    {
        seq_printf(seq, "type         : P2P-client\n");
    }
    else if(NL80211_IFTYPE_P2P_GO == ecrnx_p2p_role_get(seq, IF_P2P))
    {
        seq_printf(seq, "type         : P2P-GO\n");
    }
    else
    {
        seq_printf(seq, "type         : \n");
    }

    return 0;
}
DEFINE_SHOW_ATTRIBUTE(p2p_info);

static int p2p0_ht_info_show(struct seq_file *seq, void *v)
{
    struct cfg80211_chan_def chandef;
    u32 bw_array[]={20, 20, 40, 80, 80, 160, 5, 10};

    if (0 == ecrnx_channel_get(seq, IF_P2P, &chandef))
        seq_printf(seq, "bandwidth    : %d MHz\n", bw_array[chandef.width]);
    else
        seq_printf(seq, "bandwidth    : \n");

    return 0;
}
DEFINE_SHOW_ATTRIBUTE(p2p0_ht_info);

struct dentry *p2p0_dir = NULL;
struct dentry *p2p0_survey_info, *p2p0_p2p_info, *p2p0_ht_info;

int ecrnx_debugfs_p2p0_init(void *private_data)
{
    if (!ecr6600u_dir)
        return -ENOENT;

    p2p0_dir = debugfs_create_dir("p2p0", ecr6600u_dir);
    if (!p2p0_dir)
        return -ENOENT;

    p2p0_survey_info = debugfs_create_file("survey_info", 0444, p2p0_dir, private_data, &p2p0_survey_info_fops);
    if (!p2p0_survey_info)
        goto Fail;

    p2p0_p2p_info = debugfs_create_file("p2p_info", 0444, p2p0_dir, private_data, &p2p_info_fops);
    if (!p2p0_p2p_info)
        goto Fail;

    p2p0_ht_info = debugfs_create_file("ht_info", 0444, p2p0_dir, private_data, &p2p0_ht_info_fops);
    if (!p2p0_ht_info)
        goto Fail;

    return 0;

Fail:
    debugfs_remove_recursive(p2p0_dir);
    p2p0_dir = NULL;
    return -ENOENT;
}

void exrnx_debugfs_sruvey_info_tbl_init(void *private_data)
{
	struct ecrnx_hw *ecrnx_hw = (struct ecrnx_hw*)private_data;
	INIT_LIST_HEAD(&ecrnx_hw->debugfs_survey_info_tbl_ptr);
}

int ecrnx_debugfs_init(void     *private_data)
{
    ecrnx_debugfs_info_init(private_data);
    ecrnx_debugfs_hw_init(private_data);
    ecrnx_debugfs_wlan0_init(private_data);
    ecrnx_debugfs_ap0_init(private_data);
    ecrnx_debugfs_p2p0_init(private_data);
    exrnx_debugfs_sruvey_info_tbl_init(private_data);

    memset(&debugfs_info, 0, sizeof(debugfs_info_t));
    init_waitqueue_head(&debugfs_resp.rxdataq);

    return 0;
}
#ifdef CONFIG_ECRNX_ESWIN_USB
extern struct ring_buffer buf_handle;
#endif

void ecrnx_debugfs_exit(void)  
{
    if (ecr6600u_dir != NULL)
        debugfs_remove_recursive(ecr6600u_dir);
#ifdef CONFIG_ECRNX_ESWIN_USB
    ring_buffer_deinit(&buf_handle);
#endif

}
#endif


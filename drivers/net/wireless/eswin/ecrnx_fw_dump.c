/**
 ******************************************************************************
 *
 * @file ecrnx_fw_dump.c
 *
 * @brief Definition of debug fs entries to process fw dump
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */


#include <linux/kmod.h>
#include <linux/debugfs.h>

#include "ecrnx_defs.h"
#include "ecrnx_debugfs.h"

static ssize_t ecrnx_dbgfs_rhd_read(struct file *file,
                                   char __user *user_buf,
                                   size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    struct dbg_debug_dump_tag *dump = priv->dbgdump_elem.buf.addr;
    ssize_t read;

    mutex_lock(&priv->dbgdump_elem.mutex);
    if (!priv->debugfs.trace_prst) {
        mutex_unlock(&priv->dbgdump_elem.mutex);
        return 0;
    }

    read = simple_read_from_buffer(user_buf, count, ppos,
                                   dump->rhd_mem,
                                   dump->dbg_info.rhd_len);

    mutex_unlock(&priv->dbgdump_elem.mutex);
    return read;
}

DEBUGFS_READ_FILE_OPS(rhd);

static ssize_t ecrnx_dbgfs_rbd_read(struct file *file,
                                   char __user *user_buf,
                                   size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    struct dbg_debug_dump_tag *dump = priv->dbgdump_elem.buf.addr;
    ssize_t read;

    mutex_lock(&priv->dbgdump_elem.mutex);
    if (!priv->debugfs.trace_prst) {
        mutex_unlock(&priv->dbgdump_elem.mutex);
        return 0;
    }

    read = simple_read_from_buffer(user_buf, count, ppos,
                                   dump->rbd_mem,
                                   dump->dbg_info.rbd_len);

    mutex_unlock(&priv->dbgdump_elem.mutex);
    return read;
}

DEBUGFS_READ_FILE_OPS(rbd);

static ssize_t ecrnx_dbgfs_thdx_read(struct file *file, char __user *user_buf,
                                    size_t count, loff_t *ppos, int idx)
{
    struct ecrnx_hw *priv = file->private_data;
    struct dbg_debug_dump_tag *dump = priv->dbgdump_elem.buf.addr;
    ssize_t read;

    mutex_lock(&priv->dbgdump_elem.mutex);
    if (!priv->debugfs.trace_prst) {
        mutex_unlock(&priv->dbgdump_elem.mutex);
        return 0;
    }

    read = simple_read_from_buffer(user_buf, count, ppos,
                                   &dump->thd_mem[idx],
                                   dump->dbg_info.thd_len[idx]);

    mutex_unlock(&priv->dbgdump_elem.mutex);
    return read;
}

static ssize_t ecrnx_dbgfs_thd0_read(struct file *file,
                                    char __user *user_buf,
                                    size_t count, loff_t *ppos)
{
    return ecrnx_dbgfs_thdx_read(file, user_buf, count, ppos, 0);
}
DEBUGFS_READ_FILE_OPS(thd0);

static ssize_t ecrnx_dbgfs_thd1_read(struct file *file,
                                    char __user *user_buf,
                                    size_t count, loff_t *ppos)
{
    return ecrnx_dbgfs_thdx_read(file, user_buf, count, ppos, 1);
}
DEBUGFS_READ_FILE_OPS(thd1);

static ssize_t ecrnx_dbgfs_thd2_read(struct file *file,
                                    char __user *user_buf,
                                    size_t count, loff_t *ppos)
{
    return ecrnx_dbgfs_thdx_read(file, user_buf, count, ppos, 2);
}
DEBUGFS_READ_FILE_OPS(thd2);

static ssize_t ecrnx_dbgfs_thd3_read(struct file *file,
                                    char __user *user_buf,
                                    size_t count, loff_t *ppos)
{
    return ecrnx_dbgfs_thdx_read(file, user_buf, count, ppos, 3);
}
DEBUGFS_READ_FILE_OPS(thd3);

#if (NX_TXQ_CNT == 5)
static ssize_t ecrnx_dbgfs_thd4_read(struct file *file,
                                    char __user *user_buf,
                                    size_t count, loff_t *ppos)
{
    return ecrnx_dbgfs_thdx_read(file, user_buf, count, ppos, 4);
}
DEBUGFS_READ_FILE_OPS(thd4);
#endif

static ssize_t ecrnx_dbgfs_mactrace_read(struct file *file,
                                        char __user *user_buf,
                                        size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    struct dbg_debug_dump_tag *dump = priv->dbgdump_elem.buf.addr;
    ssize_t read;

    mutex_lock(&priv->dbgdump_elem.mutex);
    if (!priv->debugfs.trace_prst) {
        char msg[64];
        mutex_unlock(&priv->dbgdump_elem.mutex);
        scnprintf(msg, sizeof(msg), "Force trigger\n");
        ecrnx_dbgfs_trigger_fw_dump(priv, msg);

        return 0;
    }

    read = simple_read_from_buffer(user_buf, count, ppos,
                                  dump->la_mem,
                                  dump->dbg_info.la_conf.trace_len);

    mutex_unlock(&priv->dbgdump_elem.mutex);

    return read;
}
DEBUGFS_READ_FILE_OPS(mactrace);

static ssize_t ecrnx_dbgfs_macdiags_read(struct file *file,
                                        char __user *user_buf,
                                        size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    struct dbg_debug_dump_tag *dump = priv->dbgdump_elem.buf.addr;
    ssize_t read;

    mutex_lock(&priv->dbgdump_elem.mutex);
    if (!priv->debugfs.trace_prst) {
        mutex_unlock(&priv->dbgdump_elem.mutex);
        return 0;
    }

    read = simple_read_from_buffer(user_buf, count, ppos,
                                   dump->dbg_info.diags_mac,
                                   DBG_DIAGS_MAC_MAX * 2);

    mutex_unlock(&priv->dbgdump_elem.mutex);
    return read;
}

DEBUGFS_READ_FILE_OPS(macdiags);

static ssize_t ecrnx_dbgfs_phydiags_read(struct file *file,
                                        char __user *user_buf,
                                        size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    struct dbg_debug_dump_tag *dump = priv->dbgdump_elem.buf.addr;
    ssize_t read;

    mutex_lock(&priv->dbgdump_elem.mutex);
    if (!priv->debugfs.trace_prst) {
        mutex_unlock(&priv->dbgdump_elem.mutex);
        return 0;
    }

    read = simple_read_from_buffer(user_buf, count, ppos,
                                   dump->dbg_info.diags_phy,
                                   DBG_DIAGS_PHY_MAX * 2);

    mutex_unlock(&priv->dbgdump_elem.mutex);
    return read;
}

DEBUGFS_READ_FILE_OPS(phydiags);

static ssize_t ecrnx_dbgfs_hwdiags_read(struct file *file,
                                       char __user *user_buf,
                                       size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    struct dbg_debug_dump_tag *dump = priv->dbgdump_elem.buf.addr;
    char buf[16];
    int ret;

    mutex_lock(&priv->dbgdump_elem.mutex);
    if (!priv->debugfs.trace_prst) {
        mutex_unlock(&priv->dbgdump_elem.mutex);
        return 0;
    }

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "%08X\n", dump->dbg_info.hw_diag);

    mutex_unlock(&priv->dbgdump_elem.mutex);
    return simple_read_from_buffer(user_buf, count, ppos, buf, ret);
}

DEBUGFS_READ_FILE_OPS(hwdiags);

static ssize_t ecrnx_dbgfs_plfdiags_read(struct file *file,
                                       char __user *user_buf,
                                       size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    struct dbg_debug_dump_tag *dump = priv->dbgdump_elem.buf.addr;
    char buf[16];
    int ret;

    mutex_lock(&priv->dbgdump_elem.mutex);
    if (!priv->debugfs.trace_prst) {
        mutex_unlock(&priv->dbgdump_elem.mutex);
        return 0;
    }

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "%08X\n", dump->dbg_info.la_conf.diag_conf);

    mutex_unlock(&priv->dbgdump_elem.mutex);
    return simple_read_from_buffer(user_buf, count, ppos, buf, ret);
}

DEBUGFS_READ_FILE_OPS(plfdiags);

static ssize_t ecrnx_dbgfs_swdiags_read(struct file *file,
                                      char __user *user_buf,
                                      size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    struct dbg_debug_dump_tag *dump = priv->dbgdump_elem.buf.addr;
    ssize_t read;

    mutex_lock(&priv->dbgdump_elem.mutex);
    if (!priv->debugfs.trace_prst) {
        mutex_unlock(&priv->dbgdump_elem.mutex);
        return 0;
    }

    read = simple_read_from_buffer(user_buf, count, ppos,
                                   &dump->dbg_info.sw_diag,
                                   dump->dbg_info.sw_diag_len);

    mutex_unlock(&priv->dbgdump_elem.mutex);
    return read;
}

DEBUGFS_READ_FILE_OPS(swdiags);

static ssize_t ecrnx_dbgfs_error_read(struct file *file,
                                     char __user *user_buf,
                                     size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    struct dbg_debug_dump_tag *dump = priv->dbgdump_elem.buf.addr;
    ssize_t read;

    mutex_lock(&priv->dbgdump_elem.mutex);
    if (!priv->debugfs.trace_prst) {
        mutex_unlock(&priv->dbgdump_elem.mutex);
        return 0;
    }

    read = simple_read_from_buffer(user_buf, count, ppos,
                                   dump->dbg_info.error,
                                   strlen((char *)dump->dbg_info.error));

    mutex_unlock(&priv->dbgdump_elem.mutex);
    return read;
}

DEBUGFS_READ_FILE_OPS(error);

static ssize_t ecrnx_dbgfs_rxdesc_read(struct file *file,
                                      char __user *user_buf,
                                      size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    struct dbg_debug_dump_tag *dump = priv->dbgdump_elem.buf.addr;
    char buf[32];
    int ret;
    ssize_t read;

    mutex_lock(&priv->dbgdump_elem.mutex);
    if (!priv->debugfs.trace_prst) {
        mutex_unlock(&priv->dbgdump_elem.mutex);
        return 0;
    }

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "%08X\n%08X\n", dump->dbg_info.rhd,
                    dump->dbg_info.rbd);
    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    mutex_unlock(&priv->dbgdump_elem.mutex);
    return read;
}

DEBUGFS_READ_FILE_OPS(rxdesc);

static ssize_t ecrnx_dbgfs_txdesc_read(struct file *file,
                                      char __user *user_buf,
                                      size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    struct dbg_debug_dump_tag *dump = priv->dbgdump_elem.buf.addr;
    char buf[64];
    int len = 0;
    int i;

    mutex_lock(&priv->dbgdump_elem.mutex);
    if (!priv->debugfs.trace_prst) {
        mutex_unlock(&priv->dbgdump_elem.mutex);
        return 0;
    }

    for (i = 0; i < NX_TXQ_CNT; i++) {
        len += scnprintf(&buf[len], min_t(size_t, sizeof(buf) - len - 1, count),
                         "%08X\n", dump->dbg_info.thd[i]);
    }

    mutex_unlock(&priv->dbgdump_elem.mutex);
    return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

DEBUGFS_READ_FILE_OPS(txdesc);

static ssize_t ecrnx_dbgfs_macrxptr_read(struct file *file,
                                        char __user *user_buf,
                                        size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    struct dbg_debug_dump_tag *dump = priv->dbgdump_elem.buf.addr;
    ssize_t read;

    mutex_lock(&priv->dbgdump_elem.mutex);
    if (!priv->debugfs.trace_prst) {
        mutex_unlock(&priv->dbgdump_elem.mutex);
        return 0;
    }

    read = simple_read_from_buffer(user_buf, count, ppos,
                                   &dump->dbg_info.rhd_hw_ptr,
                                   2 * sizeof(dump->dbg_info.rhd_hw_ptr));

    mutex_unlock(&priv->dbgdump_elem.mutex);
    return read;
}

DEBUGFS_READ_FILE_OPS(macrxptr);

static ssize_t ecrnx_dbgfs_lamacconf_read(struct file *file,
                                         char __user *user_buf,
                                         size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    struct dbg_debug_dump_tag *dump = priv->dbgdump_elem.buf.addr;
    ssize_t read;

    mutex_lock(&priv->dbgdump_elem.mutex);
    if (!priv->debugfs.trace_prst) {
        mutex_unlock(&priv->dbgdump_elem.mutex);
        return 0;
    }

    read = simple_read_from_buffer(user_buf, count, ppos,
                                   dump->dbg_info.la_conf.conf,
                                   LA_CONF_LEN * 4);

    mutex_unlock(&priv->dbgdump_elem.mutex);
    return read;
}
DEBUGFS_READ_FILE_OPS(lamacconf);

static ssize_t ecrnx_dbgfs_chaninfo_read(struct file *file,
                                        char __user *user_buf,
                                        size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    struct dbg_debug_dump_tag *dump = priv->dbgdump_elem.buf.addr;
    char buf[4 * 32];
    int ret;

    mutex_lock(&priv->dbgdump_elem.mutex);
    if (!priv->debugfs.trace_prst) {
        mutex_unlock(&priv->dbgdump_elem.mutex);
        return 0;
    }

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "type:          %d\n"
                    "prim20_freq:   %d MHz\n"
                    "center1_freq:  %d MHz\n"
                    "center2_freq:  %d MHz\n",
                    (dump->dbg_info.chan_info.info1 >> 8)  & 0xFF,
                    (dump->dbg_info.chan_info.info1 >> 16) & 0xFFFF,
                    (dump->dbg_info.chan_info.info2 >> 0)  & 0xFFFF,
                    (dump->dbg_info.chan_info.info2 >> 16) & 0xFFFF);

    mutex_unlock(&priv->dbgdump_elem.mutex);
    return simple_read_from_buffer(user_buf, count, ppos, buf, ret);
}

DEBUGFS_READ_FILE_OPS(chaninfo);

static ssize_t ecrnx_dbgfs_um_helper_read(struct file *file,
                                         char __user *user_buf,
                                         size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    char buf[sizeof(priv->debugfs.helper_cmd)];
    int ret;

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "%s", priv->debugfs.helper_cmd);

    return simple_read_from_buffer(user_buf, count, ppos, buf, ret);
}

static ssize_t ecrnx_dbgfs_um_helper_write(struct file *file,
                                          const char __user *user_buf,
                                          size_t count, loff_t *ppos)
{
    struct ecrnx_hw *priv = file->private_data;
    int eobuf = min_t(size_t, sizeof(priv->debugfs.helper_cmd) - 1, count);

    priv->debugfs.helper_cmd[eobuf] = '\0';
    if (copy_from_user(priv->debugfs.helper_cmd, user_buf, eobuf))
        return -EFAULT;

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(um_helper);

/*
 * Calls a userspace pgm
 */
int ecrnx_um_helper(struct ecrnx_debugfs *ecrnx_debugfs, const char *cmd)
{
    struct ecrnx_hw *ecrnx_hw = container_of(ecrnx_debugfs, struct ecrnx_hw,
                                           debugfs);
    char *envp[] = { "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
    char **argv;
    int argc, ret;

    if (!ecrnx_debugfs->dir ||
        !strlen((cmd = cmd ? cmd : ecrnx_debugfs->helper_cmd)))
        return 0;
    argv = argv_split(in_interrupt() ? GFP_ATOMIC : GFP_KERNEL, cmd, &argc);
    if (!argc)
        return PTR_ERR(argv);

    if ((ret = call_usermodehelper(argv[0], argv, envp,
                                   UMH_WAIT_PROC | UMH_KILLABLE)))
        dev_err(ecrnx_hw->dev, "Failed to call %s (%s returned %d)\n",
               argv[0], cmd, ret);
    argv_free(argv);

    return ret;
}

static void ecrnx_um_helper_work(struct work_struct *ws)
{
    struct ecrnx_debugfs *ecrnx_debugfs = container_of(ws, struct ecrnx_debugfs,
                                                     helper_work);
    struct ecrnx_hw *ecrnx_hw = container_of(ecrnx_debugfs, struct ecrnx_hw,
                                           debugfs);
    ecrnx_um_helper(ecrnx_debugfs, NULL);
    if (!ecrnx_debugfs->unregistering)
        ecrnx_umh_done(ecrnx_hw);
    ecrnx_debugfs->helper_scheduled = false;
}

int ecrnx_trigger_um_helper(struct ecrnx_debugfs *ecrnx_debugfs)
{
    struct ecrnx_hw *ecrnx_hw = container_of(ecrnx_debugfs, struct ecrnx_hw,
                                           debugfs);

    if (ecrnx_debugfs->helper_scheduled == true) {
        dev_err(ecrnx_hw->dev, "%s: Already scheduled\n", __func__);
        return -EBUSY;
    }

    spin_lock_bh(&ecrnx_debugfs->umh_lock);
    if (ecrnx_debugfs->unregistering) {
        spin_unlock_bh(&ecrnx_debugfs->umh_lock);
        dev_err(ecrnx_hw->dev, "%s: unregistering\n", __func__);
        return -ENOENT;
    }
    ecrnx_debugfs->helper_scheduled = true;
    schedule_work(&ecrnx_debugfs->helper_work);
    spin_unlock_bh(&ecrnx_debugfs->umh_lock);

    return 0;
}
void ecrnx_wait_um_helper(struct ecrnx_hw *ecrnx_hw)
{
    flush_work(&ecrnx_hw->debugfs.helper_work);
}

int ecrnx_dbgfs_register_fw_dump(struct ecrnx_hw *ecrnx_hw,
                                struct dentry *dir_drv,
                                struct dentry *dir_diags)
{

    struct ecrnx_debugfs *ecrnx_debugfs = &ecrnx_hw->debugfs;

    BUILD_BUG_ON(sizeof(CONFIG_ECRNX_UM_HELPER_DFLT) >=
                 sizeof(ecrnx_debugfs->helper_cmd));
    strncpy(ecrnx_debugfs->helper_cmd,
            CONFIG_ECRNX_UM_HELPER_DFLT, sizeof(ecrnx_debugfs->helper_cmd));
    INIT_WORK(&ecrnx_debugfs->helper_work, ecrnx_um_helper_work);
    DEBUGFS_ADD_FILE(um_helper, dir_drv, S_IWUSR | S_IRUSR);

    ecrnx_debugfs->trace_prst = ecrnx_debugfs->helper_scheduled = false;
    spin_lock_init(&ecrnx_debugfs->umh_lock);
    DEBUGFS_ADD_FILE(rhd,       dir_diags, S_IRUSR);
    DEBUGFS_ADD_FILE(rbd,       dir_diags, S_IRUSR);
    DEBUGFS_ADD_FILE(thd0,      dir_diags, S_IRUSR);
    DEBUGFS_ADD_FILE(thd1,      dir_diags, S_IRUSR);
    DEBUGFS_ADD_FILE(thd2,      dir_diags, S_IRUSR);
    DEBUGFS_ADD_FILE(thd3,      dir_diags, S_IRUSR);
#if (NX_TXQ_CNT == 5)
    DEBUGFS_ADD_FILE(thd4,      dir_diags, S_IRUSR);
#endif
    DEBUGFS_ADD_FILE(mactrace,  dir_diags, S_IRUSR);
    DEBUGFS_ADD_FILE(macdiags,  dir_diags, S_IRUSR);
    DEBUGFS_ADD_FILE(phydiags,  dir_diags, S_IRUSR);
    DEBUGFS_ADD_FILE(plfdiags,  dir_diags, S_IRUSR);
    DEBUGFS_ADD_FILE(hwdiags,   dir_diags, S_IRUSR);
    DEBUGFS_ADD_FILE(swdiags,   dir_diags, S_IRUSR);
    DEBUGFS_ADD_FILE(error,     dir_diags, S_IRUSR);
    DEBUGFS_ADD_FILE(rxdesc,    dir_diags, S_IRUSR);
    DEBUGFS_ADD_FILE(txdesc,    dir_diags, S_IRUSR);
    DEBUGFS_ADD_FILE(macrxptr,  dir_diags, S_IRUSR);
    DEBUGFS_ADD_FILE(lamacconf, dir_diags, S_IRUSR);
    DEBUGFS_ADD_FILE(chaninfo,  dir_diags, S_IRUSR);

    return 0;

  err:
    return -1;
}

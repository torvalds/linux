/**
 ******************************************************************************
 *
 * @file ecrnx_debugfs.h
 *
 * @brief Miscellaneous utility function definitions
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */


#ifndef _ECRNX_DEBUGFS_H_
#define _ECRNX_DEBUGFS_H_

#include <linux/workqueue.h>
#include <linux/if_ether.h>
#include "ecrnx_fw_trace.h"

struct ecrnx_hw;
struct ecrnx_sta;

#define DEBUGFS_ADD_FILE(name, parent, mode) do {                  \
        struct dentry *__tmp;                                      \
        __tmp = debugfs_create_file(#name, mode, parent, ecrnx_hw,  \
                                    &ecrnx_dbgfs_##name##_ops);     \
        if (IS_ERR_OR_NULL(__tmp))                                 \
            goto err;                                              \
    } while (0)

#define DEBUGFS_ADD_BOOL(name, parent, ptr) do {                            \
        struct dentry *__tmp;                                               \
        __tmp = debugfs_create_bool(#name, S_IWUSR | S_IRUSR, parent, ptr); \
        if (IS_ERR_OR_NULL(__tmp))                                          \
            goto err;                                                       \
    } while (0)

#define DEBUGFS_ADD_X64(name, parent, ptr) do {                         \
        debugfs_create_x64(#name, S_IWUSR | S_IRUSR,parent, ptr);       \
    } while (0)

#define DEBUGFS_ADD_U64(name, parent, ptr, mode) do {           \
        debugfs_create_u64(#name, mode, parent, ptr);           \
    } while (0)

#define DEBUGFS_ADD_X32(name, parent, ptr) do {                         \
        debugfs_create_x32(#name, S_IWUSR | S_IRUSR, parent, ptr);      \
    } while (0)

#define DEBUGFS_ADD_U32(name, parent, ptr, mode) do {           \
        debugfs_create_u32(#name, mode, parent, ptr);           \
    } while (0)


/* file operation */
#define DEBUGFS_READ_FUNC(name)                                         \
    static ssize_t ecrnx_dbgfs_##name##_read(struct file *file,          \
                                            char __user *user_buf,      \
                                            size_t count, loff_t *ppos);

#define DEBUGFS_WRITE_FUNC(name)                                         \
    static ssize_t ecrnx_dbgfs_##name##_write(struct file *file,          \
                                             const char __user *user_buf,\
                                             size_t count, loff_t *ppos);

#define DEBUGFS_OPEN_FUNC(name)                              \
    static int ecrnx_dbgfs_##name##_open(struct inode *inode, \
                                        struct file *file);

#define DEBUGFS_RELEASE_FUNC(name)                              \
    static int ecrnx_dbgfs_##name##_release(struct inode *inode, \
                                           struct file *file);

#define DEBUGFS_READ_FILE_OPS(name)                             \
    DEBUGFS_READ_FUNC(name);                                    \
static const struct file_operations ecrnx_dbgfs_##name##_ops = { \
    .read   = ecrnx_dbgfs_##name##_read,                         \
    .open   = simple_open,                                      \
    .llseek = generic_file_llseek,                              \
};

#define DEBUGFS_WRITE_FILE_OPS(name)                            \
    DEBUGFS_WRITE_FUNC(name);                                   \
static const struct file_operations ecrnx_dbgfs_##name##_ops = { \
    .write  = ecrnx_dbgfs_##name##_write,                        \
    .open   = simple_open,                                      \
    .llseek = generic_file_llseek,                              \
};

#define DEBUGFS_READ_WRITE_FILE_OPS(name)                       \
    DEBUGFS_READ_FUNC(name);                                    \
    DEBUGFS_WRITE_FUNC(name);                                   \
static const struct file_operations ecrnx_dbgfs_##name##_ops = { \
    .write  = ecrnx_dbgfs_##name##_write,                        \
    .read   = ecrnx_dbgfs_##name##_read,                         \
    .open   = simple_open,                                      \
    .llseek = generic_file_llseek,                              \
};

#define DEBUGFS_READ_WRITE_OPEN_RELEASE_FILE_OPS(name)              \
    DEBUGFS_READ_FUNC(name);                                        \
    DEBUGFS_WRITE_FUNC(name);                                       \
    DEBUGFS_OPEN_FUNC(name);                                        \
    DEBUGFS_RELEASE_FUNC(name);                                     \
static const struct file_operations ecrnx_dbgfs_##name##_ops = {     \
    .write   = ecrnx_dbgfs_##name##_write,                           \
    .read    = ecrnx_dbgfs_##name##_read,                            \
    .open    = ecrnx_dbgfs_##name##_open,                            \
    .release = ecrnx_dbgfs_##name##_release,                         \
    .llseek  = generic_file_llseek,                                 \
};


#ifdef CONFIG_ECRNX_DEBUGFS

struct ecrnx_debugfs {
    unsigned long long rateidx;
    struct dentry *dir;
    struct dentry *dir_stas;
    bool trace_prst;

    char helper_cmd[64];
    struct work_struct helper_work;
    bool helper_scheduled;
    spinlock_t umh_lock;
    bool unregistering;

#ifndef CONFIG_ECRNX_FHOST
    struct ecrnx_fw_trace fw_trace;
#endif /* CONFIG_ECRNX_FHOST */

#ifdef CONFIG_ECRNX_FULLMAC
    struct work_struct sta_work;
    struct dentry *dir_sta[NX_REMOTE_STA_MAX];
    uint8_t sta_idx;
    struct dentry *dir_rc;
    struct dentry *dir_rc_sta[NX_REMOTE_STA_MAX];
    int rc_config[NX_REMOTE_STA_MAX];
    struct list_head rc_config_save;
    struct dentry *dir_twt;
    struct dentry *dir_twt_sta[NX_REMOTE_STA_MAX];
#endif
};

#ifdef CONFIG_ECRNX_FULLMAC

// Max duration in msecs to save rate config for a sta after disconnection
#define RC_CONFIG_DUR 600000

struct ecrnx_rc_config_save {
    struct list_head list;
    unsigned long timestamp;
    int rate;
    u8 mac_addr[ETH_ALEN];
};
#endif

int ecrnx_dbgfs_register(struct ecrnx_hw *ecrnx_hw, const char *name);
void ecrnx_dbgfs_unregister(struct ecrnx_hw *ecrnx_hw);
int ecrnx_um_helper(struct ecrnx_debugfs *ecrnx_debugfs, const char *cmd);
int ecrnx_trigger_um_helper(struct ecrnx_debugfs *ecrnx_debugfs);
void ecrnx_wait_um_helper(struct ecrnx_hw *ecrnx_hw);
#ifdef CONFIG_ECRNX_FULLMAC
void ecrnx_dbgfs_register_sta(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *sta);
void ecrnx_dbgfs_unregister_sta(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *sta);
#endif

int ecrnx_dbgfs_register_fw_dump(struct ecrnx_hw *ecrnx_hw,
                                struct dentry *dir_drv,
                                struct dentry *dir_diags);
void ecrnx_dbgfs_trigger_fw_dump(struct ecrnx_hw *ecrnx_hw, char *reason);

void ecrnx_fw_trace_dump(struct ecrnx_hw *ecrnx_hw);
void ecrnx_fw_trace_reset(struct ecrnx_hw *ecrnx_hw);

#else

struct ecrnx_debugfs {
};

static inline int ecrnx_dbgfs_register(struct ecrnx_hw *ecrnx_hw, const char *name) { return 0; }
static inline void ecrnx_dbgfs_unregister(struct ecrnx_hw *ecrnx_hw) {}
static inline int ecrnx_um_helper(struct ecrnx_debugfs *ecrnx_debugfs, const char *cmd) { return 0; }
static inline int ecrnx_trigger_um_helper(struct ecrnx_debugfs *ecrnx_debugfs) {return 0;}
static inline void ecrnx_wait_um_helper(struct ecrnx_hw *ecrnx_hw) {}
#ifdef CONFIG_ECRNX_FULLMAC
static inline void ecrnx_dbgfs_register_sta(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *sta)  {}
static inline void ecrnx_dbgfs_unregister_sta(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *sta)  {}
#endif

static inline void ecrnx_fw_trace_dump(struct ecrnx_hw *ecrnx_hw) {}
static inline void ecrnx_fw_trace_reset(struct ecrnx_hw *ecrnx_hw) {}

#endif /* CONFIG_ECRNX_DEBUGFS */


#endif /* _ECRNX_DEBUGFS_H_ */

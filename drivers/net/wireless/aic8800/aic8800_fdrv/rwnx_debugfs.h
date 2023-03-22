/**
 ******************************************************************************
 *
 * @file rwnx_debugfs.h
 *
 * @brief Miscellaneous utility function definitions
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ******************************************************************************
 */


#ifndef _RWNX_DEBUGFS_H_
#define _RWNX_DEBUGFS_H_

#include <linux/workqueue.h>
#include <linux/if_ether.h>
#include <linux/version.h>
#include "rwnx_fw_trace.h"

struct rwnx_hw;
struct rwnx_sta;

/* some macros taken from iwlwifi */
/* TODO: replace with generic read and fill read buffer in open to avoid double
 * reads */
#define DEBUGFS_ADD_FILE(name, parent, mode) do {               \
    if (!debugfs_create_file(#name, mode, parent, rwnx_hw,      \
                &rwnx_dbgfs_##name##_ops))                      \
    goto err;                                                   \
} while (0)

#define DEBUGFS_ADD_BOOL(name, parent, ptr) do {                \
    struct dentry *__tmp;                                       \
    __tmp = debugfs_create_bool(#name, S_IWUSR | S_IRUSR,       \
            parent, ptr);                                       \
    if (IS_ERR(__tmp) || !__tmp)                                \
    goto err;                                                   \
} while (0)

#define DEBUGFS_ADD_X64(name, parent, ptr) do {                 \
    struct dentry *__tmp;                                       \
    __tmp = debugfs_create_x64(#name, S_IWUSR | S_IRUSR,        \
            parent, ptr);                                       \
    if (IS_ERR(__tmp) || !__tmp)                                \
    goto err;                                                   \
} while (0)

#define DEBUGFS_ADD_U64(name, parent, ptr, mode) do {           \
    struct dentry *__tmp;                                       \
    __tmp = debugfs_create_u64(#name, mode,                     \
            parent, ptr);                                       \
    if (IS_ERR(__tmp) || !__tmp)                                \
    goto err;                                                   \
} while (0)

#define DEBUGFS_ADD_X32(name, parent, ptr) do {                 \
    struct dentry *__tmp;                                       \
    __tmp = debugfs_create_x32(#name, S_IWUSR | S_IRUSR,        \
            parent, ptr);                                       \
    if (IS_ERR(__tmp) || !__tmp)                                \
    goto err;                                                   \
} while (0)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 7, 0)
#define DEBUGFS_ADD_U32(name, parent, ptr, mode) do {           \
	debugfs_create_u32(#name, mode,                     		\
            parent, ptr);                                       \
} while (0)
#else
#define DEBUGFS_ADD_U32(name, parent, ptr, mode) do {           	\
		struct dentry *__tmp;										\
		__tmp = debugfs_create_u32(#name, mode, 					\
				parent, ptr);										\
		if (IS_ERR(__tmp) || !__tmp)								\
		goto err;													\
	} while (0)
#endif


/* file operation */
#define DEBUGFS_READ_FUNC(name)                                         \
    static ssize_t rwnx_dbgfs_##name##_read(struct file *file,          \
                                            char __user *user_buf,      \
                                            size_t count, loff_t *ppos);

#define DEBUGFS_WRITE_FUNC(name)                                         \
    static ssize_t rwnx_dbgfs_##name##_write(struct file *file,          \
                                             const char __user *user_buf,\
                                             size_t count, loff_t *ppos);

#define DEBUGFS_OPEN_FUNC(name)                              \
    static int rwnx_dbgfs_##name##_open(struct inode *inode, \
                                        struct file *file);

#define DEBUGFS_RELEASE_FUNC(name)                              \
    static int rwnx_dbgfs_##name##_release(struct inode *inode, \
                                           struct file *file);

#define DEBUGFS_READ_FILE_OPS(name)                             \
    DEBUGFS_READ_FUNC(name);                                    \
static const struct file_operations rwnx_dbgfs_##name##_ops = { \
    .read   = rwnx_dbgfs_##name##_read,                         \
    .open   = simple_open,                                      \
    .llseek = generic_file_llseek,                              \
};

#define DEBUGFS_WRITE_FILE_OPS(name)                            \
    DEBUGFS_WRITE_FUNC(name);                                   \
static const struct file_operations rwnx_dbgfs_##name##_ops = { \
    .write  = rwnx_dbgfs_##name##_write,                        \
    .open   = simple_open,                                      \
    .llseek = generic_file_llseek,                              \
};

#define DEBUGFS_READ_WRITE_FILE_OPS(name)                       \
    DEBUGFS_READ_FUNC(name);                                    \
    DEBUGFS_WRITE_FUNC(name);                                   \
static const struct file_operations rwnx_dbgfs_##name##_ops = { \
    .write  = rwnx_dbgfs_##name##_write,                        \
    .read   = rwnx_dbgfs_##name##_read,                         \
    .open   = simple_open,                                      \
    .llseek = generic_file_llseek,                              \
};

#define DEBUGFS_READ_WRITE_OPEN_RELEASE_FILE_OPS(name)              \
    DEBUGFS_READ_FUNC(name);                                        \
    DEBUGFS_WRITE_FUNC(name);                                       \
    DEBUGFS_OPEN_FUNC(name);                                        \
    DEBUGFS_RELEASE_FUNC(name);                                     \
static const struct file_operations rwnx_dbgfs_##name##_ops = {     \
    .write   = rwnx_dbgfs_##name##_write,                           \
    .read    = rwnx_dbgfs_##name##_read,                            \
    .open    = rwnx_dbgfs_##name##_open,                            \
    .release = rwnx_dbgfs_##name##_release,                         \
    .llseek  = generic_file_llseek,                                 \
};


#ifdef CONFIG_RWNX_DEBUGFS

struct rwnx_debugfs {
    unsigned long long rateidx;
    struct dentry *dir;
    bool trace_prst;

    char helper_cmd[64];
    //struct work_struct helper_work;
    bool helper_scheduled;
    spinlock_t umh_lock;
    bool unregistering;

#ifndef CONFIG_RWNX_FHOST
    struct rwnx_fw_log fw_log;
#endif /* CONFIG_RWNX_FHOST */

#ifdef CONFIG_RWNX_FULLMAC
    struct work_struct rc_stat_work;
    uint8_t rc_sta[NX_REMOTE_STA_MAX];
    uint8_t rc_write;
    uint8_t rc_read;
    struct dentry *dir_rc;
    struct dentry *dir_sta[NX_REMOTE_STA_MAX];
    int rc_config[NX_REMOTE_STA_MAX];
    struct list_head rc_config_save;
#endif
};

#ifdef CONFIG_RWNX_FULLMAC

// Max duration in msecs to save rate config for a sta after disconnection
#define RC_CONFIG_DUR 600000

struct rwnx_rc_config_save {
    struct list_head list;
    unsigned long timestamp;
    int rate;
    u8 mac_addr[ETH_ALEN];
};
#endif

int rwnx_dbgfs_register(struct rwnx_hw *rwnx_hw, const char *name);
void rwnx_dbgfs_unregister(struct rwnx_hw *rwnx_hw);
#ifdef CONFIG_RWNX_FULLMAC
void rwnx_dbgfs_register_rc_stat(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta);
void rwnx_dbgfs_unregister_rc_stat(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta);
#endif
#else

struct rwnx_debugfs {
};

static inline int rwnx_dbgfs_register(struct rwnx_hw *rwnx_hw, const char *name) { return 0; }
static inline void rwnx_dbgfs_unregister(struct rwnx_hw *rwnx_hw) {}
#ifdef CONFIG_RWNX_FULLMAC
static inline void rwnx_dbgfs_register_rc_stat(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta)  {}
static inline void rwnx_dbgfs_unregister_rc_stat(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta)  {}
#endif
#endif /* CONFIG_RWNX_DEBUGFS */


#endif /* _RWNX_DEBUGFS_H_ */

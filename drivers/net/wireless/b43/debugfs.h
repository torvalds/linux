#ifndef B43_DEBUGFS_H_
#define B43_DEBUGFS_H_

struct b43_wldev;
struct b43_txstatus;

enum b43_dyndbg {		/* Dynamic debugging features */
	B43_DBG_XMITPOWER,
	B43_DBG_DMAOVERFLOW,
	B43_DBG_DMAVERBOSE,
	B43_DBG_PWORK_FAST,
	B43_DBG_PWORK_STOP,
	__B43_NR_DYNDBG,
};

#ifdef CONFIG_B43_DEBUG

struct dentry;

#define B43_NR_LOGGED_TXSTATUS	100

struct b43_txstatus_log {
	struct b43_txstatus *log;
	int end;
	spinlock_t lock;
};

struct b43_dfs_file {
	struct dentry *dentry;
	char *buffer;
	size_t data_len;
};

struct b43_dfsentry {
	struct b43_wldev *dev;
	struct dentry *subdir;

	struct b43_dfs_file file_tsf;
	struct b43_dfs_file file_ucode_regs;
	struct b43_dfs_file file_shm;
	struct b43_dfs_file file_txstat;
	struct b43_dfs_file file_txpower_g;
	struct b43_dfs_file file_restart;
	struct b43_dfs_file file_loctls;

	struct b43_txstatus_log txstatlog;

	/* Enabled/Disabled list for the dynamic debugging features. */
	u32 dyn_debug[__B43_NR_DYNDBG];
	/* Dentries for the dynamic debugging entries. */
	struct dentry *dyn_debug_dentries[__B43_NR_DYNDBG];
};

int b43_debug(struct b43_wldev *dev, enum b43_dyndbg feature);

void b43_debugfs_init(void);
void b43_debugfs_exit(void);
void b43_debugfs_add_device(struct b43_wldev *dev);
void b43_debugfs_remove_device(struct b43_wldev *dev);
void b43_debugfs_log_txstat(struct b43_wldev *dev,
			    const struct b43_txstatus *status);

#else /* CONFIG_B43_DEBUG */

static inline int b43_debug(struct b43_wldev *dev, enum b43_dyndbg feature)
{
	return 0;
}

static inline void b43_debugfs_init(void)
{
}
static inline void b43_debugfs_exit(void)
{
}
static inline void b43_debugfs_add_device(struct b43_wldev *dev)
{
}
static inline void b43_debugfs_remove_device(struct b43_wldev *dev)
{
}
static inline void b43_debugfs_log_txstat(struct b43_wldev *dev,
					  const struct b43_txstatus *status)
{
}

#endif /* CONFIG_B43_DEBUG */

#endif /* B43_DEBUGFS_H_ */

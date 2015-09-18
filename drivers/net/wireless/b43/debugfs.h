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
	B43_DBG_LO,
	B43_DBG_FIRMWARE,
	B43_DBG_KEYS,
	B43_DBG_VERBOSESTATS,
	__B43_NR_DYNDBG,
};

#ifdef CONFIG_B43_DEBUG

struct dentry;

#define B43_NR_LOGGED_TXSTATUS	100

struct b43_txstatus_log {
	/* This structure is protected by wl->mutex */

	struct b43_txstatus *log;
	int end;
};

struct b43_dfs_file {
	struct dentry *dentry;
	char *buffer;
	size_t data_len;
};

struct b43_dfsentry {
	struct b43_wldev *dev;
	struct dentry *subdir;

	struct b43_dfs_file file_shm16read;
	struct b43_dfs_file file_shm16write;
	struct b43_dfs_file file_shm32read;
	struct b43_dfs_file file_shm32write;
	struct b43_dfs_file file_mmio16read;
	struct b43_dfs_file file_mmio16write;
	struct b43_dfs_file file_mmio32read;
	struct b43_dfs_file file_mmio32write;
	struct b43_dfs_file file_txstat;
	struct b43_dfs_file file_txpower_g;
	struct b43_dfs_file file_restart;
	struct b43_dfs_file file_loctls;

	struct b43_txstatus_log txstatlog;

	/* The cached address for the next mmio16read file read */
	u16 mmio16read_next;
	/* The cached address for the next mmio32read file read */
	u16 mmio32read_next;

	/* The cached address for the next shm16read file read */
	u32 shm16read_routing_next;
	u32 shm16read_addr_next;
	/* The cached address for the next shm32read file read */
	u32 shm32read_routing_next;
	u32 shm32read_addr_next;

	/* Enabled/Disabled list for the dynamic debugging features. */
	u32 dyn_debug[__B43_NR_DYNDBG];
	/* Dentries for the dynamic debugging entries. */
	struct dentry *dyn_debug_dentries[__B43_NR_DYNDBG];
};

bool b43_debug(struct b43_wldev *dev, enum b43_dyndbg feature);

void b43_debugfs_init(void);
void b43_debugfs_exit(void);
void b43_debugfs_add_device(struct b43_wldev *dev);
void b43_debugfs_remove_device(struct b43_wldev *dev);
void b43_debugfs_log_txstat(struct b43_wldev *dev,
			    const struct b43_txstatus *status);

#else /* CONFIG_B43_DEBUG */

static inline bool b43_debug(struct b43_wldev *dev, enum b43_dyndbg feature)
{
	return false;
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

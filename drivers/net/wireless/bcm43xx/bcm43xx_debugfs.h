#ifndef BCM43xx_DEBUGFS_H_
#define BCM43xx_DEBUGFS_H_

struct bcm43xx_private;
struct bcm43xx_xmitstatus;

#ifdef CONFIG_BCM43XX_DEBUG

#include <linux/list.h>
#include <asm/semaphore.h>

struct dentry;

/* limited by the size of the "really_big_buffer" */
#define BCM43xx_NR_LOGGED_XMITSTATUS	100

struct bcm43xx_dfsentry {
	struct dentry *subdir;
	struct dentry *dentry_devinfo;
	struct dentry *dentry_spromdump;
	struct dentry *dentry_tsf;
	struct dentry *dentry_txstat;

	struct bcm43xx_private *bcm;

	/* saved xmitstatus. */
	struct bcm43xx_xmitstatus *xmitstatus_buffer;
	int xmitstatus_ptr;
	int xmitstatus_cnt;
	/* We need a seperate buffer while printing to avoid
	 * concurrency issues. (New xmitstatus can arrive
	 * while we are printing).
	 */
	struct bcm43xx_xmitstatus *xmitstatus_print_buffer;
	int saved_xmitstatus_ptr;
	int saved_xmitstatus_cnt;
	int xmitstatus_printing;
};

struct bcm43xx_debugfs {
	struct dentry *root;
	struct dentry *dentry_driverinfo;
};

void bcm43xx_debugfs_init(void);
void bcm43xx_debugfs_exit(void);
void bcm43xx_debugfs_add_device(struct bcm43xx_private *bcm);
void bcm43xx_debugfs_remove_device(struct bcm43xx_private *bcm);
void bcm43xx_debugfs_log_txstat(struct bcm43xx_private *bcm,
				struct bcm43xx_xmitstatus *status);

/* Debug helper: Dump binary data through printk. */
void bcm43xx_printk_dump(const char *data,
			 size_t size,
			 const char *description);
/* Debug helper: Dump bitwise binary data through printk. */
void bcm43xx_printk_bitdump(const unsigned char *data,
			    size_t bytes, int msb_to_lsb,
			    const char *description);
#define bcm43xx_printk_bitdumpt(pointer, msb_to_lsb, description) \
	do {									\
		bcm43xx_printk_bitdump((const unsigned char *)(pointer),	\
				       sizeof(*(pointer)),			\
				       (msb_to_lsb),				\
				       (description));				\
	} while (0)

#else /* CONFIG_BCM43XX_DEBUG*/

static inline
void bcm43xx_debugfs_init(void) { }
static inline
void bcm43xx_debugfs_exit(void) { }
static inline
void bcm43xx_debugfs_add_device(struct bcm43xx_private *bcm) { }
static inline
void bcm43xx_debugfs_remove_device(struct bcm43xx_private *bcm) { }
static inline
void bcm43xx_debugfs_log_txstat(struct bcm43xx_private *bcm,
				struct bcm43xx_xmitstatus *status) { }

static inline
void bcm43xx_printk_dump(const char *data,
			 size_t size,
			 const char *description)
{
}
static inline
void bcm43xx_printk_bitdump(const unsigned char *data,
			    size_t bytes, int msb_to_lsb,
			    const char *description)
{
}
#define bcm43xx_printk_bitdumpt(pointer, msb_to_lsb, description)  do { /* nothing */ } while (0)

#endif /* CONFIG_BCM43XX_DEBUG*/

/* Ugly helper macros to make incomplete code more verbose on runtime */
#ifdef TODO
# undef TODO
#endif
#define TODO()  \
	do {										\
		printk(KERN_INFO PFX "TODO: Incomplete code in %s() at %s:%d\n",	\
		       __FUNCTION__, __FILE__, __LINE__);				\
	} while (0)

#ifdef FIXME
# undef FIXME
#endif
#define FIXME()  \
	do {										\
		printk(KERN_INFO PFX "FIXME: Possibly broken code in %s() at %s:%d\n",	\
		       __FUNCTION__, __FILE__, __LINE__);				\
	} while (0)

#endif /* BCM43xx_DEBUGFS_H_ */

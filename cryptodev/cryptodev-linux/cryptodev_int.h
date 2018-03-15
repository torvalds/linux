/* cipher stuff */
#ifndef CRYPTODEV_INT_H
# define CRYPTODEV_INT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
#  define reinit_completion(x) INIT_COMPLETION(*(x))
#endif

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/scatterlist.h>
#include <crypto/cryptodev.h>
#include <crypto/aead.h>

#define PFX "cryptodev: "
#define dprintk(level, severity, format, a...)			\
	do {							\
		if (level <= cryptodev_verbosity)		\
			printk(severity PFX "%s[%u] (%s:%u): " format "\n",	\
			       current->comm, current->pid,	\
			       __func__, __LINE__,		\
			       ##a);				\
	} while (0)
#define derr(level, format, a...) dprintk(level, KERN_ERR, format, ##a)
#define dwarning(level, format, a...) dprintk(level, KERN_WARNING, format, ##a)
#define dinfo(level, format, a...) dprintk(level, KERN_INFO, format, ##a)
#define ddebug(level, format, a...) dprintk(level, KERN_DEBUG, format, ##a)


extern int cryptodev_verbosity;

struct fcrypt {
	struct list_head list;
	struct mutex sem;
};

/* compatibility stuff */
#ifdef CONFIG_COMPAT
#include <linux/compat.h>

/* input of CIOCGSESSION */
struct compat_session_op {
	/* Specify either cipher or mac
	 */
	uint32_t	cipher;		/* cryptodev_crypto_op_t */
	uint32_t	mac;		/* cryptodev_crypto_op_t */

	uint32_t	keylen;
	compat_uptr_t	key;		/* pointer to key data */
	uint32_t	mackeylen;
	compat_uptr_t	mackey;		/* pointer to mac key data */

	uint32_t	ses;		/* session identifier */
};

/* input of CIOCCRYPT */
struct compat_crypt_op {
	uint32_t	ses;		/* session identifier */
	uint16_t	op;		/* COP_ENCRYPT or COP_DECRYPT */
	uint16_t	flags;		/* see COP_FLAG_* */
	uint32_t	len;		/* length of source data */
	compat_uptr_t	src;		/* source data */
	compat_uptr_t	dst;		/* pointer to output data */
	compat_uptr_t	mac;/* pointer to output data for hash/MAC operations */
	compat_uptr_t	iv;/* initialization vector for encryption operations */
};

/* compat ioctls, defined for the above structs */
#define COMPAT_CIOCGSESSION    _IOWR('c', 102, struct compat_session_op)
#define COMPAT_CIOCCRYPT       _IOWR('c', 104, struct compat_crypt_op)
#define COMPAT_CIOCASYNCCRYPT  _IOW('c', 107, struct compat_crypt_op)
#define COMPAT_CIOCASYNCFETCH  _IOR('c', 108, struct compat_crypt_op)

#endif /* CONFIG_COMPAT */

/* kernel-internal extension to struct crypt_op */
struct kernel_crypt_op {
	struct crypt_op cop;

	int ivlen;
	__u8 iv[EALG_MAX_BLOCK_LEN];

	int digestsize;
	uint8_t hash_output[AALG_MAX_RESULT_LEN];

	struct task_struct *task;
	struct mm_struct *mm;
};

struct kernel_crypt_auth_op {
	struct crypt_auth_op caop;

	int dst_len; /* based on src_len + pad + tag */
	int ivlen;
	__u8 iv[EALG_MAX_BLOCK_LEN];

	struct task_struct *task;
	struct mm_struct *mm;
};

/* auth */

int kcaop_from_user(struct kernel_crypt_auth_op *kcop,
			struct fcrypt *fcr, void __user *arg);
int kcaop_to_user(struct kernel_crypt_auth_op *kcaop,
		struct fcrypt *fcr, void __user *arg);
int crypto_auth_run(struct fcrypt *fcr, struct kernel_crypt_auth_op *kcaop);
int crypto_run(struct fcrypt *fcr, struct kernel_crypt_op *kcop);

#include <cryptlib.h>

/* other internal structs */
struct csession {
	struct list_head entry;
	struct mutex sem;
	struct cipher_data cdata;
	struct hash_data hdata;
	uint32_t sid;
	uint32_t alignmask;

	unsigned int array_size;
	unsigned int used_pages; /* the number of pages that are used */
	/* the number of pages marked as NOT-writable; they preceed writeables */
	unsigned int readonly_pages;
	struct page **pages;
	struct scatterlist *sg;
};

struct csession *crypto_get_session_by_sid(struct fcrypt *fcr, uint32_t sid);

static inline void crypto_put_session(struct csession *ses_ptr)
{
	mutex_unlock(&ses_ptr->sem);
}
int adjust_sg_array(struct csession *ses, int pagecount);

#endif /* CRYPTODEV_INT_H */

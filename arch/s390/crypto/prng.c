/*
 * Copyright IBM Corp. 2006,2007
 * Author(s): Jan Glauber <jan.glauber@de.ibm.com>
 * Driver for the s390 pseudo random number generator
 */
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/random.h>
#include <asm/debug.h>
#include <asm/uaccess.h>

#include "crypt_s390.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jan Glauber <jan.glauber@de.ibm.com>");
MODULE_DESCRIPTION("s390 PRNG interface");

static int prng_chunk_size = 256;
module_param(prng_chunk_size, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(prng_chunk_size, "PRNG read chunk size in bytes");

static int prng_entropy_limit = 4096;
module_param(prng_entropy_limit, int, S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR);
MODULE_PARM_DESC(prng_entropy_limit,
	"PRNG add entropy after that much bytes were produced");

/*
 * Any one who considers arithmetical methods of producing random digits is,
 * of course, in a state of sin. -- John von Neumann
 */

struct s390_prng_data {
	unsigned long count; /* how many bytes were produced */
	char *buf;
};

static struct s390_prng_data *p;

/* copied from libica, use a non-zero initial parameter block */
static unsigned char parm_block[32] = {
0x0F,0x2B,0x8E,0x63,0x8C,0x8E,0xD2,0x52,0x64,0xB7,0xA0,0x7B,0x75,0x28,0xB8,0xF4,
0x75,0x5F,0xD2,0xA6,0x8D,0x97,0x11,0xFF,0x49,0xD8,0x23,0xF3,0x7E,0x21,0xEC,0xA0,
};

static int prng_open(struct inode *inode, struct file *file)
{
	return nonseekable_open(inode, file);
}

static void prng_add_entropy(void)
{
	__u64 entropy[4];
	unsigned int i;
	int ret;

	for (i = 0; i < 16; i++) {
		ret = crypt_s390_kmc(KMC_PRNG, parm_block, (char *)entropy,
				     (char *)entropy, sizeof(entropy));
		BUG_ON(ret < 0 || ret != sizeof(entropy));
		memcpy(parm_block, entropy, sizeof(entropy));
	}
}

static void prng_seed(int nbytes)
{
	char buf[16];
	int i = 0;

	BUG_ON(nbytes > 16);
	get_random_bytes(buf, nbytes);

	/* Add the entropy */
	while (nbytes >= 8) {
		*((__u64 *)parm_block) ^= *((__u64 *)buf+i*8);
		prng_add_entropy();
		i += 8;
		nbytes -= 8;
	}
	prng_add_entropy();
}

static ssize_t prng_read(struct file *file, char __user *ubuf, size_t nbytes,
			 loff_t *ppos)
{
	int chunk, n;
	int ret = 0;
	int tmp;

	/* nbytes can be arbitrary long, we spilt it into chunks */
	while (nbytes) {
		/* same as in extract_entropy_user in random.c */
		if (need_resched()) {
			if (signal_pending(current)) {
				if (ret == 0)
					ret = -ERESTARTSYS;
				break;
			}
			schedule();
		}

		/*
		 * we lose some random bytes if an attacker issues
		 * reads < 8 bytes, but we don't care
		 */
		chunk = min_t(int, nbytes, prng_chunk_size);

		/* PRNG only likes multiples of 8 bytes */
		n = (chunk + 7) & -8;

		if (p->count > prng_entropy_limit)
			prng_seed(8);

		/* if the CPU supports PRNG stckf is present too */
		asm volatile(".insn     s,0xb27c0000,%0"
			     : "=m" (*((unsigned long long *)p->buf)) : : "cc");

		/*
		 * Beside the STCKF the input for the TDES-EDE is the output
		 * of the last operation. We differ here from X9.17 since we
		 * only store one timestamp into the buffer. Padding the whole
		 * buffer with timestamps does not improve security, since
		 * successive stckf have nearly constant offsets.
		 * If an attacker knows the first timestamp it would be
		 * trivial to guess the additional values. One timestamp
		 * is therefore enough and still guarantees unique input values.
		 *
		 * Note: you can still get strict X9.17 conformity by setting
		 * prng_chunk_size to 8 bytes.
		*/
		tmp = crypt_s390_kmc(KMC_PRNG, parm_block, p->buf, p->buf, n);
		BUG_ON((tmp < 0) || (tmp != n));

		p->count += n;

		if (copy_to_user(ubuf, p->buf, chunk))
			return -EFAULT;

		nbytes -= chunk;
		ret += chunk;
		ubuf += chunk;
	}
	return ret;
}

static struct file_operations prng_fops = {
	.owner		= THIS_MODULE,
	.open		= &prng_open,
	.release	= NULL,
	.read		= &prng_read,
};

static struct miscdevice prng_dev = {
	.name	= "prandom",
	.minor	= MISC_DYNAMIC_MINOR,
	.fops	= &prng_fops,
};

static int __init prng_init(void)
{
	int ret;

	/* check if the CPU has a PRNG */
	if (!crypt_s390_func_available(KMC_PRNG))
		return -EOPNOTSUPP;

	if (prng_chunk_size < 8)
		return -EINVAL;

	p = kmalloc(sizeof(struct s390_prng_data), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->count = 0;

	p->buf = kmalloc(prng_chunk_size, GFP_KERNEL);
	if (!p->buf) {
		ret = -ENOMEM;
		goto out_free;
	}

	/* initialize the PRNG, add 128 bits of entropy */
	prng_seed(16);

	ret = misc_register(&prng_dev);
	if (ret) {
		printk(KERN_WARNING
		       "Could not register misc device for PRNG.\n");
		goto out_buf;
	}
	return 0;

out_buf:
	kfree(p->buf);
out_free:
	kfree(p);
	return ret;
}

static void __exit prng_exit(void)
{
	/* wipe me */
	memset(p->buf, 0, prng_chunk_size);
	kfree(p->buf);
	kfree(p);

	misc_deregister(&prng_dev);
}

module_init(prng_init);
module_exit(prng_exit);


#ifndef __NX_842_H__
#define __NX_842_H__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nx842.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/io.h>

struct nx842_driver {
	struct module *owner;

	struct nx842_constraints *constraints;

	int (*compress)(const unsigned char *in, unsigned int in_len,
			unsigned char *out, unsigned int *out_len,
			void *wrkmem);
	int (*decompress)(const unsigned char *in, unsigned int in_len,
			  unsigned char *out, unsigned int *out_len,
			  void *wrkmem);
};

void nx842_register_driver(struct nx842_driver *driver);
void nx842_unregister_driver(struct nx842_driver *driver);


/* To allow the main nx-compress module to load platform module */
#define NX842_PSERIES_MODULE_NAME	"nx-compress-pseries"
#define NX842_PSERIES_COMPAT_NAME	"ibm,compression"


#endif /* __NX_842_H__ */

#include <linux/fs.h>

extern const struct file_operations xen_privcmd_fops;
extern const struct file_operations xen_privcmdbuf_fops;

extern struct miscdevice xen_privcmdbuf_dev;

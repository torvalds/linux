
#ifndef _COMEDI_FOPS_H
#define _COMEDI_FOPS_H
#include <linux/types.h>

extern struct class *comedi_class;
extern const struct file_operations comedi_fops;
extern bool comedi_autoconfig;
extern struct comedi_driver *comedi_drivers;

#endif /* _COMEDI_FOPS_H */

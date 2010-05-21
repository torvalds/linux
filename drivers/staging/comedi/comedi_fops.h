
#ifndef _COMEDI_FOPS_H
#define _COMEDI_FOPS_H

extern struct class *comedi_class;
extern const struct file_operations comedi_fops;
extern int comedi_autoconfig;
extern struct comedi_driver *comedi_drivers;

#endif /* _COMEDI_FOPS_H */

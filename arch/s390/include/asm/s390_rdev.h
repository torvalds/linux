/*
 *  include/asm-s390/ccwdev.h
 *
 *    Copyright (C) 2002,2005 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *               Carsten Otte  <cotte@de.ibm.com>
 *
 *  Interface for s390 root device
 */

#ifndef _S390_RDEV_H_
#define _S390_RDEV_H_
extern struct device *s390_root_dev_register(const char *);
extern void s390_root_dev_unregister(struct device *);
#endif /* _S390_RDEV_H_ */

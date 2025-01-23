/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  KVM guest address space mapping code
 *
 *    Copyright IBM Corp. 2007, 2016, 2025
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 *               Claudio Imbrenda <imbrenda@linux.ibm.com>
 */

#ifndef ARCH_KVM_S390_GMAP_H
#define ARCH_KVM_S390_GMAP_H

int gmap_make_secure(struct gmap *gmap, unsigned long gaddr, void *uvcb);
int gmap_convert_to_secure(struct gmap *gmap, unsigned long gaddr);
int gmap_destroy_page(struct gmap *gmap, unsigned long gaddr);

#endif

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

#define GMAP_SHADOW_FAKE_TABLE 1ULL

int gmap_make_secure(struct gmap *gmap, unsigned long gaddr, void *uvcb);
int gmap_convert_to_secure(struct gmap *gmap, unsigned long gaddr);
int gmap_destroy_page(struct gmap *gmap, unsigned long gaddr);
struct gmap *gmap_shadow(struct gmap *parent, unsigned long asce, int edat_level);

/**
 * gmap_shadow_valid - check if a shadow guest address space matches the
 *                     given properties and is still valid
 * @sg: pointer to the shadow guest address space structure
 * @asce: ASCE for which the shadow table is requested
 * @edat_level: edat level to be used for the shadow translation
 *
 * Returns 1 if the gmap shadow is still valid and matches the given
 * properties, the caller can continue using it. Returns 0 otherwise, the
 * caller has to request a new shadow gmap in this case.
 *
 */
static inline int gmap_shadow_valid(struct gmap *sg, unsigned long asce, int edat_level)
{
	if (sg->removed)
		return 0;
	return sg->orig_asce == asce && sg->edat_level == edat_level;
}

#endif

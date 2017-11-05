/*
 * Copyright (C) 2012 Kent Yoder IBM Corporation
 *
 * HWRNG interfaces to pull RNG data from a TPM
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/module.h>
#include <linux/hw_random.h>
#include <linux/tpm.h>

#define MODULE_NAME "tpm-rng"

static int tpm_rng_read(struct hwrng *rng, void *data, size_t max, bool wait)
{
	return tpm_get_random(NULL, data, max);
}

static struct hwrng tpm_rng = {
	.name = MODULE_NAME,
	.read = tpm_rng_read,
};

static int __init rng_init(void)
{
	return hwrng_register(&tpm_rng);
}
module_init(rng_init);

static void __exit rng_exit(void)
{
	hwrng_unregister(&tpm_rng);
}
module_exit(rng_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Kent Yoder <key@linux.vnet.ibm.com>");
MODULE_DESCRIPTION("RNG driver for TPM devices");

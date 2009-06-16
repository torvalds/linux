/*
 * DaVinci DM6446 EVM board specific headers
 *
 * Author: Kevin Hilman, Deep Root Systems, LLC
 *
 * 2007 (c) Deep Root Systems, LLC. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or ifndef.
 */

#ifndef _MACH_DAVINCI_DM6446EVM_H
#define _MACH_DAVINCI_DM6446EVM_H

#include <linux/types.h>

int dm6446evm_eeprom_read(char *buf, off_t off, size_t count);
int dm6446evm_eeprom_write(char *buf, off_t off, size_t count);

#endif

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#define __IN_STRING_C

#include <linux/module.h>
#include <linux/string.h>

char *strcpy(char *dest, const char *src)
{
	return __kernel_strcpy(dest, src);
}
EXPORT_SYMBOL(strcpy);

char *strcat(char *dest, const char *src)
{
	return __kernel_strcpy(dest + strlen(dest), src);
}
EXPORT_SYMBOL(strcat);

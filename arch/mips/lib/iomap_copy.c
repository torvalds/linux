/*
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <linux/export.h>
#include <linux/io.h>

/**
 * __ioread64_copy - copy data from MMIO space, in 64-bit units
 * @to: destination (must be 64-bit aligned)
 * @from: source, in MMIO space (must be 64-bit aligned)
 * @count: number of 64-bit quantities to copy
 *
 * Copy data from MMIO space to kernel space, in units of 32 or 64 bits at a
 * time.  Order of access is not guaranteed, nor is a memory barrier
 * performed afterwards.
 */
void __ioread64_copy(void *to, const void __iomem *from, size_t count)
{
#ifdef CONFIG_64BIT
	u64 *dst = to;
	const u64 __iomem *src = from;
	const u64 __iomem *end = src + count;

	while (src < end)
		*dst++ = __raw_readq(src++);
#else
	__ioread32_copy(to, from, count * 2);
#endif
}
EXPORT_SYMBOL_GPL(__ioread64_copy);

/*
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_EXCEPTION_H
#define __ASM_EXCEPTION_H

struct pt_regs;
struct exception_table_entry;

extern void ia64_handle_exception(struct pt_regs *regs,
				  const struct exception_table_entry *e);

#define ia64_done_with_exception(regs)					  \
({									  \
	int __ex_ret = 0;						  \
	const struct exception_table_entry *e;				  \
	e = search_exception_tables((regs)->cr_iip + ia64_psr(regs)->ri); \
	if (e) {							  \
		ia64_handle_exception(regs, e);				  \
		__ex_ret = 1;						  \
	}								  \
	__ex_ret;							  \
})

#endif	/* __ASM_EXCEPTION_H */

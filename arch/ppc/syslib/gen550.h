/*
 * gen550 prototypes
 *
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * 2004 (c) MontaVista Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

extern void gen550_progress(char *, unsigned short);
extern void gen550_init(int, struct uart_port *);
extern void gen550_kgdb_map_scc(void);

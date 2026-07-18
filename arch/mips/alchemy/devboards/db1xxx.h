/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_MIPS_DB1XXX__
#define __LINUX_MIPS_DB1XXX__

const char *get_system_type(void);
int db1000_board_setup(void);
int db1000_dev_setup(void);
int db1500_pci_setup(void);
int db1200_board_setup(void);
int db1200_dev_setup(void);
int db1300_board_setup(void);
int db1300_dev_setup(void);
int db1550_board_setup(void);
int db1550_dev_setup(void);
int db1550_pci_setup(int id);

#endif /* __LINUX_MIPS_DB1XXX__ */

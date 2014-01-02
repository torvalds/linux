/*
 * DB1200/PB1200 / DB1550 / DB1300 board support.
 *
 * These 4 boards can reliably be supported in a single kernel image.
 */

#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-db1x00/bcsr.h>

int __init db1200_board_setup(void);
int __init db1200_dev_setup(void);
int __init db1300_board_setup(void);
int __init db1300_dev_setup(void);
int __init db1550_board_setup(void);
int __init db1550_dev_setup(void);
int __init db1550_pci_setup(int);

static const char *board_type_str(void)
{
	switch (BCSR_WHOAMI_BOARD(bcsr_read(BCSR_WHOAMI))) {
	case BCSR_WHOAMI_PB1200_DDR1:
	case BCSR_WHOAMI_PB1200_DDR2:
		return "PB1200";
	case BCSR_WHOAMI_DB1200:
		return "DB1200";
	case BCSR_WHOAMI_DB1300:
		return "DB1300";
	case BCSR_WHOAMI_DB1550:
		return "DB1550";
	case BCSR_WHOAMI_PB1550_SDR:
	case BCSR_WHOAMI_PB1550_DDR:
		return "PB1550";
	default:
		return "(unknown)";
	}
}

const char *get_system_type(void)
{
	return board_type_str();
}

void __init board_setup(void)
{
	int ret;

	switch (alchemy_get_cputype()) {
	case ALCHEMY_CPU_AU1550:
		ret = db1550_board_setup();
		break;
	case ALCHEMY_CPU_AU1200:
		ret = db1200_board_setup();
		break;
	case ALCHEMY_CPU_AU1300:
		ret = db1300_board_setup();
		break;
	default:
		pr_err("unsupported CPU on board\n");
		ret = -ENODEV;
	}
	if (ret)
		panic("cannot initialize board support");
}

int __init db1235_arch_init(void)
{
	int id = BCSR_WHOAMI_BOARD(bcsr_read(BCSR_WHOAMI));
	if (id == BCSR_WHOAMI_DB1550)
		return db1550_pci_setup(0);
	else if ((id == BCSR_WHOAMI_PB1550_SDR) ||
		 (id == BCSR_WHOAMI_PB1550_DDR))
		return db1550_pci_setup(1);

	return 0;
}
arch_initcall(db1235_arch_init);

int __init db1235_dev_init(void)
{
	switch (BCSR_WHOAMI_BOARD(bcsr_read(BCSR_WHOAMI))) {
	case BCSR_WHOAMI_PB1200_DDR1:
	case BCSR_WHOAMI_PB1200_DDR2:
	case BCSR_WHOAMI_DB1200:
		return db1200_dev_setup();
	case BCSR_WHOAMI_DB1300:
		return db1300_dev_setup();
	case BCSR_WHOAMI_DB1550:
	case BCSR_WHOAMI_PB1550_SDR:
	case BCSR_WHOAMI_PB1550_DDR:
		return db1550_dev_setup();
	}
	return 0;
}
device_initcall(db1235_dev_init);

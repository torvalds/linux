/* arch/arm/mach-s3c2410/include/mach/nand.h
 *
 * Copyright (c) 2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - NAND device controller platfrom_device info
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/**
 * struct s3c2410_nand_set - define a set of one or more nand chips
 * @disable_ecc:	Entirely disable ECC - Dangerous
 * @flash_bbt: 		Openmoko u-boot can create a Bad Block Table
 *			Setting this flag will allow the kernel to
 *			look for it at boot time and also skip the NAND
 *			scan.
 * @nr_chips:		Number of chips in this set
 * @nr_partitions:	Number of partitions pointed to by @partitions
 * @name:		Name of set (optional)
 * @nr_map:		Map for low-layer logical to physical chip numbers (option)
 * @partitions:		The mtd partition list
 *
 * define a set of one or more nand chips registered with an unique mtd. Also
 * allows to pass flag to the underlying NAND layer. 'disable_ecc' will trigger
 * a warning at boot time.
 */
struct s3c2410_nand_set {
	unsigned int		disable_ecc:1;
	unsigned int		flash_bbt:1;

	int			nr_chips;
	int			nr_partitions;
	char			*name;
	int			*nr_map;
	struct mtd_partition	*partitions;
	struct nand_ecclayout	*ecc_layout;
};

struct s3c2410_platform_nand {
	/* timing information for controller, all times in nanoseconds */

	int	tacls;	/* time for active CLE/ALE to nWE/nOE */
	int	twrph0;	/* active time for nWE/nOE */
	int	twrph1;	/* time for release CLE/ALE from nWE/nOE inactive */

	unsigned int	ignore_unset_ecc:1;

	int			nr_sets;
	struct s3c2410_nand_set *sets;

	void			(*select_chip)(struct s3c2410_nand_set *,
					       int chip);
};


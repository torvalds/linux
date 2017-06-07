/*
 * © Copyright 2016 ATMEL
 * © Copyright 2016 Free Electrons
 *
 * Author: Boris Brezillon <boris.brezillon@free-electrons.com>
 *
 * Derived from the atmel_nand.c driver which contained the following
 * copyrights:
 *
 *    Copyright © 2003 Rick Bronson
 *
 *    Derived from drivers/mtd/nand/autcpu12.c
 *        Copyright © 2001 Thomas Gleixner (gleixner@autronix.de)
 *
 *    Derived from drivers/mtd/spia.c
 *        Copyright © 2000 Steven J. Hill (sjhill@cotw.com)
 *
 *
 *    Add Hardware ECC support for AT91SAM9260 / AT91SAM9263
 *        Richard Genoud (richard.genoud@gmail.com), Adeneo Copyright © 2007
 *
 *        Derived from Das U-Boot source code
 *              (u-boot-1.1.5/board/atmel/at91sam9263ek/nand.c)
 *        © Copyright 2006 ATMEL Rousset, Lacressonniere Nicolas
 *
 *    Add Programmable Multibit ECC support for various AT91 SoC
 *        © Copyright 2012 ATMEL, Hong Xu
 *
 *    Add Nand Flash Controller support for SAMA5 SoC
 *        © Copyright 2013 ATMEL, Josh Wu (josh.wu@atmel.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef ATMEL_PMECC_H
#define ATMEL_PMECC_H

#define ATMEL_PMECC_MAXIMIZE_ECC_STRENGTH	0
#define ATMEL_PMECC_SECTOR_SIZE_AUTO		0
#define ATMEL_PMECC_OOBOFFSET_AUTO		-1

struct atmel_pmecc_user_req {
	int pagesize;
	int oobsize;
	struct {
		int strength;
		int bytes;
		int sectorsize;
		int nsectors;
		int ooboffset;
	} ecc;
};

struct atmel_pmecc *devm_atmel_pmecc_get(struct device *dev);

struct atmel_pmecc_user *
atmel_pmecc_create_user(struct atmel_pmecc *pmecc,
			struct atmel_pmecc_user_req *req);
void atmel_pmecc_destroy_user(struct atmel_pmecc_user *user);

int atmel_pmecc_enable(struct atmel_pmecc_user *user, int op);
void atmel_pmecc_disable(struct atmel_pmecc_user *user);
int atmel_pmecc_wait_rdy(struct atmel_pmecc_user *user);
int atmel_pmecc_correct_sector(struct atmel_pmecc_user *user, int sector,
			       void *data, void *ecc);
bool atmel_pmecc_correct_erased_chunks(struct atmel_pmecc_user *user);
void atmel_pmecc_get_generated_eccbytes(struct atmel_pmecc_user *user,
					int sector, void *ecc);

#endif /* ATMEL_PMECC_H */

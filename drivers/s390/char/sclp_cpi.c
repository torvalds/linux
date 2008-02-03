/*
 *  drivers/s390/char/sclp_cpi.c
 *    SCLP control programm identification
 *
 *    Copyright IBM Corp. 2001, 2007
 *    Author(s): Martin Peschke <mpeschke@de.ibm.com>
 *		 Michael Ernst <mernst@de.ibm.com>
 */

#include <linux/kmod.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include "sclp_cpi_sys.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Identify this operating system instance "
		   "to the System z hardware");
MODULE_AUTHOR("Martin Peschke <mpeschke@de.ibm.com>, "
	      "Michael Ernst <mernst@de.ibm.com>");

static char *system_name = "";
static char *sysplex_name = "";

module_param(system_name, charp, 0);
MODULE_PARM_DESC(system_name, "e.g. hostname - max. 8 characters");
module_param(sysplex_name, charp, 0);
MODULE_PARM_DESC(sysplex_name, "if applicable - max. 8 characters");

static int __init cpi_module_init(void)
{
	return sclp_cpi_set_data(system_name, sysplex_name, "LINUX",
				 LINUX_VERSION_CODE);
}

static void __exit cpi_module_exit(void)
{
}

module_init(cpi_module_init);
module_exit(cpi_module_exit);

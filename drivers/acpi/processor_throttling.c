/*
 * processor_throttling.c - Throttling submodule of the ACPI processor driver
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2004       Dominik Brodowski <linux@brodo.de>
 *  Copyright (C) 2004  Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 *  			- Added processor hotplug support
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include <acpi/acpi_bus.h>
#include <acpi/processor.h>

#define ACPI_PROCESSOR_COMPONENT        0x01000000
#define ACPI_PROCESSOR_CLASS            "processor"
#define ACPI_PROCESSOR_DRIVER_NAME      "ACPI Processor Driver"
#define _COMPONENT              ACPI_PROCESSOR_COMPONENT
ACPI_MODULE_NAME("acpi_processor")

/* --------------------------------------------------------------------------
                              Throttling Control
   -------------------------------------------------------------------------- */
static int acpi_processor_get_throttling(struct acpi_processor *pr)
{
	int state = 0;
	u32 value = 0;
	u32 duty_mask = 0;
	u32 duty_value = 0;


	if (!pr)
		return -EINVAL;

	if (!pr->flags.throttling)
		return -ENODEV;

	pr->throttling.state = 0;

	duty_mask = pr->throttling.state_count - 1;

	duty_mask <<= pr->throttling.duty_offset;

	local_irq_disable();

	value = inl(pr->throttling.address);

	/*
	 * Compute the current throttling state when throttling is enabled
	 * (bit 4 is on).
	 */
	if (value & 0x10) {
		duty_value = value & duty_mask;
		duty_value >>= pr->throttling.duty_offset;

		if (duty_value)
			state = pr->throttling.state_count - duty_value;
	}

	pr->throttling.state = state;

	local_irq_enable();

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Throttling state is T%d (%d%% throttling applied)\n",
			  state, pr->throttling.states[state].performance));

	return 0;
}

int acpi_processor_set_throttling(struct acpi_processor *pr, int state)
{
	u32 value = 0;
	u32 duty_mask = 0;
	u32 duty_value = 0;


	if (!pr)
		return -EINVAL;

	if ((state < 0) || (state > (pr->throttling.state_count - 1)))
		return -EINVAL;

	if (!pr->flags.throttling)
		return -ENODEV;

	if (state == pr->throttling.state)
		return 0;

	/*
	 * Calculate the duty_value and duty_mask.
	 */
	if (state) {
		duty_value = pr->throttling.state_count - state;

		duty_value <<= pr->throttling.duty_offset;

		/* Used to clear all duty_value bits */
		duty_mask = pr->throttling.state_count - 1;

		duty_mask <<= acpi_fadt.duty_offset;
		duty_mask = ~duty_mask;
	}

	local_irq_disable();

	/*
	 * Disable throttling by writing a 0 to bit 4.  Note that we must
	 * turn it off before you can change the duty_value.
	 */
	value = inl(pr->throttling.address);
	if (value & 0x10) {
		value &= 0xFFFFFFEF;
		outl(value, pr->throttling.address);
	}

	/*
	 * Write the new duty_value and then enable throttling.  Note
	 * that a state value of 0 leaves throttling disabled.
	 */
	if (state) {
		value &= duty_mask;
		value |= duty_value;
		outl(value, pr->throttling.address);

		value |= 0x00000010;
		outl(value, pr->throttling.address);
	}

	pr->throttling.state = state;

	local_irq_enable();

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Throttling state set to T%d (%d%%)\n", state,
			  (pr->throttling.states[state].performance ? pr->
			   throttling.states[state].performance / 10 : 0)));

	return 0;
}

int acpi_processor_get_throttling_info(struct acpi_processor *pr)
{
	int result = 0;
	int step = 0;
	int i = 0;


	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "pblk_address[0x%08x] duty_offset[%d] duty_width[%d]\n",
			  pr->throttling.address,
			  pr->throttling.duty_offset,
			  pr->throttling.duty_width));

	if (!pr)
		return -EINVAL;

	/* TBD: Support ACPI 2.0 objects */

	if (!pr->throttling.address) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "No throttling register\n"));
		return 0;
	} else if (!pr->throttling.duty_width) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "No throttling states\n"));
		return 0;
	}
	/* TBD: Support duty_cycle values that span bit 4. */
	else if ((pr->throttling.duty_offset + pr->throttling.duty_width) > 4) {
		printk(KERN_WARNING PREFIX "duty_cycle spans bit 4\n");
		return 0;
	}

	/*
	 * PIIX4 Errata: We don't support throttling on the original PIIX4.
	 * This shouldn't be an issue as few (if any) mobile systems ever
	 * used this part.
	 */
	if (errata.piix4.throttle) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Throttling not supported on PIIX4 A- or B-step\n"));
		return 0;
	}

	pr->throttling.state_count = 1 << acpi_fadt.duty_width;

	/*
	 * Compute state values. Note that throttling displays a linear power/
	 * performance relationship (at 50% performance the CPU will consume
	 * 50% power).  Values are in 1/10th of a percent to preserve accuracy.
	 */

	step = (1000 / pr->throttling.state_count);

	for (i = 0; i < pr->throttling.state_count; i++) {
		pr->throttling.states[i].performance = step * i;
		pr->throttling.states[i].power = step * i;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found %d throttling states\n",
			  pr->throttling.state_count));

	pr->flags.throttling = 1;

	/*
	 * Disable throttling (if enabled).  We'll let subsequent policy (e.g.
	 * thermal) decide to lower performance if it so chooses, but for now
	 * we'll crank up the speed.
	 */

	result = acpi_processor_get_throttling(pr);
	if (result)
		goto end;

	if (pr->throttling.state) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Disabling throttling (was T%d)\n",
				  pr->throttling.state));
		result = acpi_processor_set_throttling(pr, 0);
		if (result)
			goto end;
	}

      end:
	if (result)
		pr->flags.throttling = 0;

	return result;
}

/* proc interface */

static int acpi_processor_throttling_seq_show(struct seq_file *seq,
					      void *offset)
{
	struct acpi_processor *pr = seq->private;
	int i = 0;
	int result = 0;


	if (!pr)
		goto end;

	if (!(pr->throttling.state_count > 0)) {
		seq_puts(seq, "<not supported>\n");
		goto end;
	}

	result = acpi_processor_get_throttling(pr);

	if (result) {
		seq_puts(seq,
			 "Could not determine current throttling state.\n");
		goto end;
	}

	seq_printf(seq, "state count:             %d\n"
		   "active state:            T%d\n",
		   pr->throttling.state_count, pr->throttling.state);

	seq_puts(seq, "states:\n");
	for (i = 0; i < pr->throttling.state_count; i++)
		seq_printf(seq, "   %cT%d:                  %02d%%\n",
			   (i == pr->throttling.state ? '*' : ' '), i,
			   (pr->throttling.states[i].performance ? pr->
			    throttling.states[i].performance / 10 : 0));

      end:
	return 0;
}

static int acpi_processor_throttling_open_fs(struct inode *inode,
					     struct file *file)
{
	return single_open(file, acpi_processor_throttling_seq_show,
			   PDE(inode)->data);
}

static ssize_t acpi_processor_write_throttling(struct file * file,
					       const char __user * buffer,
					       size_t count, loff_t * data)
{
	int result = 0;
	struct seq_file *m = file->private_data;
	struct acpi_processor *pr = m->private;
	char state_string[12] = { '\0' };


	if (!pr || (count > sizeof(state_string) - 1))
		return -EINVAL;

	if (copy_from_user(state_string, buffer, count))
		return -EFAULT;

	state_string[count] = '\0';

	result = acpi_processor_set_throttling(pr,
					       simple_strtoul(state_string,
							      NULL, 0));
	if (result)
		return result;

	return count;
}

struct file_operations acpi_processor_throttling_fops = {
	.open = acpi_processor_throttling_open_fs,
	.read = seq_read,
	.write = acpi_processor_write_throttling,
	.llseek = seq_lseek,
	.release = single_release,
};

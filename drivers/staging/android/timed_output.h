/* include/linux/timed_output.h
 *
 * Copyright (C) 2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
*/

#ifndef _LINUX_TIMED_OUTPUT_H
#define _LINUX_TIMED_OUTPUT_H

struct timed_output_dev {
	const char	*name;

	/* enable the output and set the timer */
	void	(*enable)(struct timed_output_dev *sdev, int timeout);

	/* returns the current number of milliseconds remaining on the timer */
	int		(*get_time)(struct timed_output_dev *sdev);

	/* private data */
	struct device	*dev;
	int		index;
	int		state;
};

int timed_output_dev_register(struct timed_output_dev *dev);
void timed_output_dev_unregister(struct timed_output_dev *dev);

#endif

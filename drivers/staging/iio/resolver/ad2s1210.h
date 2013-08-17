/*
 * ad2s1210.h plaform data for the ADI Resolver to Digital Converters:
 * AD2S1210
 *
 * Copyright (c) 2010-2010 Analog Devices Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

struct ad2s1210_platform_data {
	unsigned sample;
	unsigned a[2];
	unsigned res[2];
	bool gpioin;
};

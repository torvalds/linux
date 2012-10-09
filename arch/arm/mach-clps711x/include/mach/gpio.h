/*
 *  This file contains the CLPS711X GPIO definitions.
 *
 *  Copyright (C) 2012 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Simple helper for convert port & pin to GPIO number */
#define CLPS711X_GPIO(port, bit)	((port) * 8 + (bit))

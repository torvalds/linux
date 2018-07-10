/*
 * volume_id - reads filesystem label and uuid
 *
 * Copyright (C) 2005 Kay Sievers <kay.sievers@vrfy.org>
 *
 *	This library is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU Lesser General Public
 *	License as published by the Free Software Foundation; either
 *	version 2.1 of the License, or (at your option) any later version.
 *
 *	This library is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *	Lesser General Public License for more details.
 *
 *	You should have received a copy of the GNU Lesser General Public
 *	License along with this library; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

char *get_devname_from_label(const char *spec);
char *get_devname_from_uuid(const char *spec);
void display_uuid_cache(int scan_devices);

/* Returns:
 * 0: no UUID= or LABEL= prefix found
 * 1: UUID= or LABEL= prefix found. In this case,
 *    *fsname is replaced if device with such UUID or LABEL is found
 */
int resolve_mount_spec(char **fsname);
int add_to_uuid_cache(const char *device);

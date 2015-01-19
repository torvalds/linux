/*
 * Amlogic Apollo
 * frame buffer driver
 *
 * Copyright (C) 2009 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:	Tim Yao <timyao@amlogic.com>
 *
 */

#ifndef TVMODE_H
#define TVMODE_H

typedef enum {
    TVMODE_480I  = 0,
    TVMODE_480CVBS,
    TVMODE_480P  ,
    TVMODE_576I  ,
    TVMODE_576CVBS,
    TVMODE_576P  ,
    TVMODE_720P  ,
    TVMODE_1080I ,
    TVMODE_1080P ,
    TVMODE_720P_50HZ ,
    TVMODE_1080I_50HZ ,
    TVMODE_1080P_50HZ ,
    TVMODE_MAX   
} tvmode_t;

#endif /* TVMODE_H */

/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2016 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2016 Vivante Corporation
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


#ifndef __gc_hal_statistics_h_
#define __gc_hal_statistics_h_


#define VIV_STAT_ENABLE_STATISTICS              0

/*  Toal number of frames for which the frame time is accounted. We have storage
    to keep frame times for last this many frames.
*/
#define VIV_STAT_FRAME_BUFFER_SIZE              30


/*
    Total number of frames sampled for a mode. This means

    # of frames for HZ Current  : VIV_STAT_EARLY_Z_SAMPLE_FRAMES
    # of frames for HZ Switched : VIV_STAT_EARLY_Z_SAMPLE_FRAMES
  +
  --------------------------------------------------------
                                : (2 * VIV_STAT_EARLY_Z_SAMPLE_FRAMES) frames needed

    IMPORTANT: This total must be smaller than VIV_STAT_FRAME_BUFFER_SIZE
*/
#define VIV_STAT_EARLY_Z_SAMPLE_FRAMES          7
#define VIV_STAT_EARLY_Z_LATENCY_FRAMES         2

/* Multiplication factor for previous Hz off mode. Make it more than 1.0 to advertise HZ on.*/
#define VIV_STAT_EARLY_Z_FACTOR                 (1.05f)

/* Defines the statistical data keys monitored by the statistics module */
typedef enum _gceSTATISTICS
{
    gcvFRAME_FPS        =   1,
}
gceSTATISTICS;

/* HAL statistics information. */
typedef struct _gcsSTATISTICS_EARLYZ
{
    gctUINT                     switchBackCount;
    gctUINT                     nextCheckPoint;
    gctBOOL                     disabled;
}
gcsSTATISTICS_EARLYZ;


/* HAL statistics information. */
typedef struct _gcsSTATISTICS
{
    gctUINT64                   frameTime[VIV_STAT_FRAME_BUFFER_SIZE];
    gctUINT64                   previousFrameTime;
    gctUINT                     frame;
    gcsSTATISTICS_EARLYZ        earlyZ;
}
gcsSTATISTICS;


/* Add a frame based data into current statistics. */
void
gcfSTATISTICS_AddData(
    IN gceSTATISTICS Key,
    IN gctUINT Value
    );

/* Marks the frame end and triggers statistical calculations and decisions.*/
void
gcfSTATISTICS_MarkFrameEnd (
    void
    );

/* Sets whether the dynmaic HZ is disabled or not .*/
void
gcfSTATISTICS_DisableDynamicEarlyZ (
    IN gctBOOL Disabled
    );

#endif /*__gc_hal_statistics_h_ */


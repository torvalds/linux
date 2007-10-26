/*
 * Handlers for board audio hooks, splitted from bttv-cards
 *
 * Copyright (c) 2006 Mauro Carvalho Chehab (mchehab@infradead.org)
 * This code is placed under the terms of the GNU General Public License
 */

#include "bttvp.h"

static void winview_audio(struct bttv *btv, struct video_audio *v, int set);
static void lt9415_audio(struct bttv *btv, struct video_audio *v, int set);
static void avermedia_tvphone_audio(struct bttv *btv, struct video_audio *v,
				    int set);
static void avermedia_tv_stereo_audio(struct bttv *btv, struct video_audio *v,
				      int set);
static void terratv_audio(struct bttv *btv, struct video_audio *v, int set);
static void gvbctv3pci_audio(struct bttv *btv, struct video_audio *v, int set);
static void gvbctv5pci_audio(struct bttv *btv, struct video_audio *v, int set);
static void winfast2000_audio(struct bttv *btv, struct video_audio *v, int set);
static void pvbt878p9b_audio(struct bttv *btv, struct video_audio *v, int set);
static void fv2000s_audio(struct bttv *btv, struct video_audio *v, int set);
static void windvr_audio(struct bttv *btv, struct video_audio *v, int set);
static void adtvk503_audio(struct bttv *btv, struct video_audio *v, int set);

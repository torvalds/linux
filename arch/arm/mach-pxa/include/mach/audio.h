/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ARCH_AUDIO_H__
#define __ASM_ARCH_AUDIO_H__

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>

/*
 * @reset_gpio: AC97 reset gpio (normally gpio113 or gpio95)
 *              a -1 value means no gpio will be used for reset
 * @codec_pdata: AC97 codec platform_data

 * reset_gpio should only be specified for pxa27x CPUs where a silicon
 * bug prevents correct operation of the reset line. If not specified,
 * the default behaviour on these CPUs is to consider gpio 113 as the
 * AC97 reset line, which is the default on most boards.
 */
typedef struct {
	int (*startup)(struct snd_pcm_substream *, void *);
	void (*shutdown)(struct snd_pcm_substream *, void *);
	void (*suspend)(void *);
	void (*resume)(void *);
	void *priv;
	int reset_gpio;
	void *codec_pdata[AC97_BUS_MAX_DEVICES];
} pxa2xx_audio_ops_t;

extern void pxa_set_ac97_info(pxa2xx_audio_ops_t *ops);

#endif

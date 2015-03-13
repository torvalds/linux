#ifndef __BACKPORT_SOUND_PCM_H
#define __BACKPORT_SOUND_PCM_H
#include_next <sound/pcm.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)
/**
 * snd_pcm_stop_xrun - stop the running streams as XRUN
 * @substream: the PCM substream instance
 *
 * This stops the given running substream (and all linked substreams) as XRUN.
 * Unlike snd_pcm_stop(), this function takes the substream lock by itself.
 *
 * Return: Zero if successful, or a negative error code.
 */
static inline int snd_pcm_stop_xrun(struct snd_pcm_substream *substream)
{
	unsigned long flags;
	int ret = 0;

	snd_pcm_stream_lock_irqsave(substream, flags);
	if (snd_pcm_running(substream))
		ret = snd_pcm_stop(substream, SNDRV_PCM_STATE_XRUN);
	snd_pcm_stream_unlock_irqrestore(substream, flags);
	return ret;
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0) */

#endif /* __BACKPORT_SOUND_PCM_H */

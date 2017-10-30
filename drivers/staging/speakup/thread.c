#include <linux/kthread.h>
#include <linux/wait.h>

#include "spk_types.h"
#include "speakup.h"
#include "spk_priv.h"

DECLARE_WAIT_QUEUE_HEAD(speakup_event);
EXPORT_SYMBOL_GPL(speakup_event);

int speakup_thread(void *data)
{
	unsigned long flags;
	int should_break;
	struct bleep our_sound;

	our_sound.active = 0;
	our_sound.freq = 0;
	our_sound.jiffies = 0;

	mutex_lock(&spk_mutex);
	while (1) {
		DEFINE_WAIT(wait);

		while (1) {
			spin_lock_irqsave(&speakup_info.spinlock, flags);
			our_sound = spk_unprocessed_sound;
			spk_unprocessed_sound.active = 0;
			prepare_to_wait(&speakup_event, &wait,
					TASK_INTERRUPTIBLE);
			should_break = kthread_should_stop() ||
				our_sound.active ||
				(synth && synth->catch_up && synth->alive &&
					(speakup_info.flushing ||
					!synth_buffer_empty()));
			spin_unlock_irqrestore(&speakup_info.spinlock, flags);
			if (should_break)
				break;
			mutex_unlock(&spk_mutex);
			schedule();
			mutex_lock(&spk_mutex);
		}
		finish_wait(&speakup_event, &wait);
		if (kthread_should_stop())
			break;

		if (our_sound.active)
			kd_mksound(our_sound.freq, our_sound.jiffies);
		if (synth && synth->catch_up && synth->alive) {
			/*
			 * It is up to the callee to take the lock, so that it
			 * can sleep whenever it likes
			 */
			synth->catch_up(synth);
		}

		speakup_start_ttys();
	}
	mutex_unlock(&spk_mutex);
	return 0;
}

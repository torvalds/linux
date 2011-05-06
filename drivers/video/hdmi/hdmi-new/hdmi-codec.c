#include <linux/hdmi-new.h>
extern void wm8990_set_spk(bool on);

int hdmi_codec_set_audio_fs(unsigned char audio_fs)
{
	return 0;
}
void hdmi_set_spk(int on)
{
	wm8990_set_spk(!on);
}
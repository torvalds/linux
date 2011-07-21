#include <linux/hdmi.h>
extern void codec_set_spk(bool on);

void hdmi_set_spk(int on)
{
	codec_set_spk(on);
}

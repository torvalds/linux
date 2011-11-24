#include <linux/hdmi.h>
#if defined CONFIG_SND_SOC_WM8900 || defined CONFIG_SND_SOC_RT5631 || defined CONFIG_SND_SOC_RT5621
/* sound/soc/codecs/wm8900.c */
extern void codec_set_spk(bool on);
#else
void codec_set_spk(bool on) 
{
	/* please add sound switching-related code here or on your codec driver
	   parameter: on=1 ==> open spk 
				  on=0 ==> close spk
	*/
}
#endif
void hdmi_set_spk(int on)
{
	codec_set_spk(on);
}

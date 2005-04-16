#define CS_AUDIO_UNDEFINED		0x20
#define CS_AUDIO_DEVICE			0x21
#define CS_AUDIO_CONFIGURATION		0x22
#define CS_AUDIO_STRING			0x23
#define CS_AUDIO_INTERFACE		0x24
#define CS_AUDIO_ENDPOINT		0x25

#define HEADER				0x01
#define INPUT_TERMINAL			0x02
#define OUTPUT_TERMINAL			0x03
#define MIXER_UNIT			0x04
#define SELECTOR_UNIT			0x05
#define FEATURE_UNIT			0x06
#define PROCESSING_UNIT			0x07
#define EXTENSION_UNIT			0x08

#define AS_GENERAL			0x01
#define FORMAT_TYPE			0x02
#define FORMAT_SPECIFIC			0x03

#define EP_GENERAL			0x01

#define MAX_CHAN			9
#define MAX_FREQ			16
#define MAX_IFACE			8
#define MAX_FORMAT			8
#define MAX_ALT				32 	/* Sorry, we need quite a few for the Philips webcams */

struct usb_audio_terminal
{	
	u8	flags;
	u8	assoc;
	u16	type;			/* Mic etc */
	u8	channels;
	u8	source;
	u16	chancfg;
};

struct usb_audio_format
{
	u8	type;
	u8	channels;
	u8	num_freq;
	u8	sfz;
	u8	bits;
	u16	freq[MAX_FREQ];
};

struct usb_audio_interface
{
	u8	terminal;
	u8	delay;
	u16	num_formats;
	u16	format_type;
	u8	flags;
	u8	idleconf;	/* Idle config */
#define AU_IFACE_FOUND	1
	struct  usb_audio_format format[MAX_FORMAT];
};

struct usb_audio_device
{
	struct list_head list;
	u8	mixer;
	u8	selector;
	void	*irq_handle;
	u8	num_channels;
	u8	num_dsp_iface;
	u8	channel_map[MAX_CHAN];
	struct usb_audio_terminal terminal[MAX_CHAN];
	struct usb_audio_interface interface[MAX_IFACE][MAX_ALT];
};



/* Audio Class specific Request Codes */

#define SET_CUR    0x01
#define GET_CUR    0x81
#define SET_MIN    0x02
#define GET_MIN    0x82
#define SET_MAX    0x03
#define GET_MAX    0x83
#define SET_RES    0x04
#define GET_RES    0x84
#define SET_MEM    0x05
#define GET_MEM    0x85
#define GET_STAT   0xff

/* Terminal Control Selectors */

#define COPY_PROTECT_CONTROL       0x01

/* Feature Unit Control Selectors */

#define MUTE_CONTROL               0x01
#define VOLUME_CONTROL             0x02
#define BASS_CONTROL               0x03
#define MID_CONTROL                0x04
#define TREBLE_CONTROL             0x05
#define GRAPHIC_EQUALIZER_CONTROL  0x06
#define AUTOMATIC_GAIN_CONTROL     0x07
#define DELAY_CONTROL              0x08
#define BASS_BOOST_CONTROL         0x09
#define LOUDNESS_CONTROL           0x0a

/* Endpoint Control Selectors */

#define SAMPLING_FREQ_CONTROL      0x01
#define PITCH_CONTROL              0x02

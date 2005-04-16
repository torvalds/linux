#ifndef __MXB__
#define __MXB__

#define BASE_VIDIOC_MXB 10

#define MXB_S_AUDIO_CD		_IOW  ('V', BASE_VIDIOC_PRIVATE+BASE_VIDIOC_MXB+0, int)
#define MXB_S_AUDIO_LINE	_IOW  ('V', BASE_VIDIOC_PRIVATE+BASE_VIDIOC_MXB+1, int)

#define MXB_IDENTIFIER "Multimedia eXtension Board"

#define MXB_AUDIOS	6

/* these are the available audio sources, which can switched
   to the line- and cd-output individually */
static struct v4l2_audio mxb_audios[MXB_AUDIOS] = {
	    {
		.index	= 0,
		.name	= "Tuner",
		.capability = V4L2_AUDCAP_STEREO,
	} , {
		.index	= 1,
		.name	= "AUX1",
		.capability = V4L2_AUDCAP_STEREO,
	} , {
		.index	= 2,
		.name	= "AUX2",
		.capability = V4L2_AUDCAP_STEREO,
	} , {
		.index	= 3,
		.name	= "AUX3",
		.capability = V4L2_AUDCAP_STEREO,
	} , {
		.index	= 4,
		.name	= "Radio (X9)",
		.capability = V4L2_AUDCAP_STEREO,
	} , {
		.index	= 5,
		.name	= "CD-ROM (X10)",
		.capability = V4L2_AUDCAP_STEREO,
	}
};	
#endif

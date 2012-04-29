#ifndef __MXB__
#define __MXB__

#define BASE_VIDIOC_MXB 10

#define MXB_S_AUDIO_CD		_IOW  ('V', BASE_VIDIOC_PRIVATE+BASE_VIDIOC_MXB+0, int)
#define MXB_S_AUDIO_LINE	_IOW  ('V', BASE_VIDIOC_PRIVATE+BASE_VIDIOC_MXB+1, int)

#define MXB_IDENTIFIER "Multimedia eXtension Board"

#define MXB_AUDIOS	6

#endif

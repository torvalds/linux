#ifndef ENABLE_WAIT_FORMAT
#define ENABLE_WAIT_FORMAT
#endif

#ifndef AUDIODSP_CONTROL_H
#define AUDIODSP_CONTROL_H
 

struct audiodsp_cmd 
{
int cmd;
int fmt;
int data_len;
char *data;
};


#define AUDIODSP_SYNC_AUDIO_PAUSE					_IO('a', 0x01)
#define AUDIODSP_SYNC_AUDIO_RESUME					_IO('a', 0x02)

#define AUDIODSP_SET_FMT						_IOW('a',1,long)
#define AUDIODSP_START							_IOW('a',2,long)
#define AUDIODSP_STOP							_IOW('a',3,long)
#define AUDIODSP_DECODE_START						_IOW('a',4,long)
#define AUDIODSP_DECODE_STOP						_IOW('a',5,long)
#define AUDIODSP_REGISTER_FIRMWARE					_IOW('a',6,long)
#define AUDIODSP_UNREGISTER_ALLFIRMWARE					_IOW('a',7,long)
#define AUDIODSP_SYNC_AUDIO_START					_IOW('a',8,unsigned long)
#define AUDIODSP_SYNC_AUDIO_TSTAMP_DISCONTINUITY	_IOW('a',9,unsigned long)
#define AUDIODSP_SYNC_SET_APTS						_IOW('a',10,unsigned long)

#ifdef ENABLE_WAIT_FORMAT
#define AUDIODSP_WAIT_FORMAT						_IOW('a',11,long)
#endif
#define AUDIODSP_DROP_PCMDATA					_IOW('a',12, unsigned long)

#define AUDIODSP_SKIP_BYTES                     _IOW('a', 13, unsigned long)

#define AUDIODSP_GET_CHANNELS_NUM					_IOR('r',1,long)
#define AUDIODSP_GET_SAMPLERATE						_IOR('r',2,long)
#define AUDIODSP_GET_BITS_PER_SAMPLE					_IOR('r',3,long)
#define AUDIODSP_GET_PTS						_IOR('r',4,long)
#define AUDIODSP_GET_DECODED_NB_FRAMES			_IOR('r',5,long)
#define AUDIODSP_GET_FIRST_PTS_FLAG				_IOR('r',6,long)
#define AUDIODSP_SYNC_GET_APTS					_IOR('r',7,unsigned long)
#define AUDIODSP_SYNC_GET_PCRSCR					_IOR('r',8,unsigned long)
#define AUDIODSP_AUTOMUTE_ON					_IOW('r',9,unsigned long)
#define AUDIODSP_AUTOMUTE_OFF					_IOW('r',10,unsigned long)
#define AUDIODSP_LOOKUP_APTS                                   _IOR('r',11,unsigned long)
#define AUDIODSP_GET_PCM_LEVEL					_IOR('r',12,unsigned long)
#define AUDIODSP_SET_PCM_BUF_SIZE				_IOW('r',13,long)

#define MCODEC_FMT_MPEG123 (1<<0)
#define MCODEC_FMT_AAC 	  (1<<1)
#define MCODEC_FMT_AC3 	  (1<<2)
#define MCODEC_FMT_DTS		  (1<<3)
#define MCODEC_FMT_FLAC	  (1<<4)
#define MCODEC_FMT_COOK		(1<<5)
#define MCODEC_FMT_AMR		(1<<6)
#define MCODEC_FMT_RAAC     (1<<7)
#define MCODEC_FMT_ADPCM	  (1<<8)
#define MCODEC_FMT_WMA     (1<<9)
#define MCODEC_FMT_PCM      (1<<10)
#define MCODEC_FMT_WMAPRO     (1<<11)
#define MCODEC_FMT_ALAC     (1<<12)
#define MCODEC_FMT_AAC_LATM     (1<<14)
#define MCODEC_FMT_APE     (1<<15)
#define MCODEC_FMT_EAC3     (1<<16)
#define MCODEC_FMT_NULL     (1<<17)
#define AUDIOINFO_FROM_AUDIODSP(format)  ((format == MCODEC_FMT_AAC) || \
										  (format ==MCODEC_FMT_AAC_LATM))
#endif


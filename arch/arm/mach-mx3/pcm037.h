#ifndef __PCM037_H__
#define __PCM037_H__

enum pcm037_board_variant {
	PCM037_PCM970,
	PCM037_EET,
};

extern enum pcm037_board_variant pcm037_variant(void);

#endif

/* Header for the Gemini SATA bridge */
#ifndef SATA_GEMINI_H
#define SATA_GEMINI_H

struct sata_gemini;

enum gemini_muxmode {
	GEMINI_MUXMODE_0 = 0,
	GEMINI_MUXMODE_1,
	GEMINI_MUXMODE_2,
	GEMINI_MUXMODE_3,
};

struct sata_gemini *gemini_sata_bridge_get(void);
bool gemini_sata_bridge_enabled(struct sata_gemini *sg, bool is_ata1);
enum gemini_muxmode gemini_sata_get_muxmode(struct sata_gemini *sg);
int gemini_sata_start_bridge(struct sata_gemini *sg, unsigned int bridge);
void gemini_sata_stop_bridge(struct sata_gemini *sg, unsigned int bridge);
int gemini_sata_reset_bridge(struct sata_gemini *sg, unsigned int bridge);

#endif

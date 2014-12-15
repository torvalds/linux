
#ifndef AUDIODSP_MICROCODE_HEADER
#define AUDIODSP_MICROCODE_HEADER
#include "audiodsp_module.h"
struct audiodsp_microcode
{
	int 	id;
	int 	fmt;//support format;
	struct list_head list;
	unsigned long code_start_addr;
	unsigned long code_size;
	char file_name[64];
};



extern int audiodsp_microcode_register(struct audiodsp_priv*priv,int fmt,char *filename);
extern struct audiodsp_microcode *  audiodsp_find_supoort_mcode(struct audiodsp_priv*priv,int fmt);
extern int audiodsp_microcode_load(struct audiodsp_priv*priv,struct audiodsp_microcode *pmcode);
int audiodsp_microcode_free(struct audiodsp_priv*priv);
 #endif


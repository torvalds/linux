

#define RWNX_FN_ENTRY_STR ">>> %s()\n", __func__



/* message levels */
#define LOGERROR		0x0001
#define LOGINFO			0x0002
#define LOGTRACE		0x0004
#define LOGDEBUG		0x0008
#define LOGDATA			0x0010

extern int aicwf_dbg_level;
void rwnx_data_dump(char* tag, void* data, unsigned long len);

#define AICWF_LOG		"AICWFDBG("

#define AICWFDBG(level, args, arg...)	\
do {	\
	if (aicwf_dbg_level & level) {	\
		printk(AICWF_LOG#level")\t" args, ##arg); \
	}	\
} while (0)

#define RWNX_DBG(fmt, ...)	\
do {	\
	if (aicwf_dbg_level & LOGTRACE) {	\
		printk(AICWF_LOG"LOGTRACE)\t"fmt , ##__VA_ARGS__); 	\
	}	\
} while (0)



#if 0
#define RWNX_DBG(fmt, ...)	\
	do {	\
		if (aicwf_dbg_level & LOGTRACE) {	\
			printk(AICWF_LOG"LOGTRACE"")\t" fmt, ##__VA_ARGS__); \
		}	\
	} while (0)
#define AICWFDBG(args, level)	\
do {	\
	if (aicwf_dbg_level & level) {	\
		printk(AICWF_LOG"(%s)\t" ,#level);	\
		printf args;	\
	}	\
} while (0)
#endif




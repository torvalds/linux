#ifndef _CODEC_MESSAGE_HEADERS
#define _CODEC_MESSAGE_HEADERS


#define SUB_FMT_VALID 			(1<<1)
#define CHANNEL_VALID 			(1<<2)	
#define SAMPLE_RATE_VALID     	(1<<3)	
#define DATA_WIDTH_VALID     	(1<<4)	
struct digit_raw_output_info
{
	int	framelength;
	unsigned char* framebuf;
	int frame_size;
	int frame_samples;
	unsigned char* rawptr;
    //for AC3
	int sampleratecode;
	int bsmod;

    int bpf;
    int brst;
    int length;
    int padsize;
    int mode;
    unsigned int syncword1;
    unsigned int syncword2;
    unsigned int syncword3;

    unsigned int syncword1_mask;
    unsigned int syncword2_mask;
    unsigned int syncword3_mask;

    unsigned chstat0_l;
    unsigned chstat0_r;
    unsigned chstat1_l;
    unsigned chstat1_r;

    unsigned can_bypass;
};

struct frame_fmt
{

    int valid;
    int sub_fmt;
    int channel_num;
    int sample_rate;
    int data_width;
    int buffered_len;/*dsp codec,buffered origan data len*/ 
    int format;
    unsigned int total_byte_parsed;
    union{	
    	 unsigned int total_sample_decoded;
        void  *pcm_encoded_info;	//used for encoded pcm info	 	 
    }data;		 
    unsigned int bps;
    void* private_data;
    struct digit_raw_output_info * digit_raw_output_info;
};


struct frame_info
{

    int len;
    unsigned long  offset;/*steam start to here*/
    unsigned long  buffered_len;/*data buffer in  dsp,pcm datalen*/
    int reversed[1];/*for cache aligned 32 bytes*/
};

struct dsp_working_info
{
	int status;
	int sp;
	int pc;
	int ilink1;
	int ilink2;
	int blink;
	int jeffies;
	int out_wp;
	int out_rp;
	int buffered_len;//pcm buffered at the dsp side
	int es_offset;//stream read offset since start decoder
	int reserved[5];
};
#endif


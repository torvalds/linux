/*
 * SharpSL SSP Driver
 */

struct corgissp_machinfo {
	int port;
	int cs_lcdcon;
	int cs_ads7846;
	int cs_max1111;
	int clk_lcdcon;
	int clk_ads7846;
	int clk_max1111;
};

void corgi_ssp_set_machinfo(struct corgissp_machinfo *machinfo);

/*
 * SharpSL Backlight
 */

void corgi_bl_set_intensity(int intensity);
void spitz_bl_set_intensity(int intensity);
void akita_bl_set_intensity(int intensity);

/*
 * SharpSL Touchscreen Driver
 */

unsigned long corgi_get_hsync_len(void);
unsigned long spitz_get_hsync_len(void);
void corgi_put_hsync(void);
void spitz_put_hsync(void);
void corgi_wait_hsync(void);
void spitz_wait_hsync(void);

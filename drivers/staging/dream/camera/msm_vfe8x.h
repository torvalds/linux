/*
 * Copyright (C) 2008-2009 QUALCOMM Incorporated.
 */
#ifndef __MSM_VFE8X_H__
#define __MSM_VFE8X_H__

#define TRUE  1
#define FALSE 0
#define boolean uint8_t

enum  VFE_STATE {
	VFE_STATE_IDLE,
	VFE_STATE_ACTIVE
};

enum vfe_cmd_id {
	/*
	*Important! Command_ID are arranged in order.
	*Don't change!*/
	VFE_CMD_ID_START,
	VFE_CMD_ID_RESET,

	/* bus and camif config */
	VFE_CMD_ID_AXI_INPUT_CONFIG,
	VFE_CMD_ID_CAMIF_CONFIG,
	VFE_CMD_ID_AXI_OUTPUT_CONFIG,

	/* module config  */
	VFE_CMD_ID_BLACK_LEVEL_CONFIG,
	VFE_CMD_ID_ROLL_OFF_CONFIG,
	VFE_CMD_ID_DEMUX_CHANNEL_GAIN_CONFIG,
	VFE_CMD_ID_DEMOSAIC_CONFIG,
	VFE_CMD_ID_FOV_CROP_CONFIG,
	VFE_CMD_ID_MAIN_SCALER_CONFIG,
	VFE_CMD_ID_WHITE_BALANCE_CONFIG,
	VFE_CMD_ID_COLOR_CORRECTION_CONFIG,
	VFE_CMD_ID_LA_CONFIG,
	VFE_CMD_ID_RGB_GAMMA_CONFIG,
	VFE_CMD_ID_CHROMA_ENHAN_CONFIG,
	VFE_CMD_ID_CHROMA_SUPPRESSION_CONFIG,
	VFE_CMD_ID_ASF_CONFIG,
	VFE_CMD_ID_SCALER2Y_CONFIG,
	VFE_CMD_ID_SCALER2CbCr_CONFIG,
	VFE_CMD_ID_CHROMA_SUBSAMPLE_CONFIG,
	VFE_CMD_ID_FRAME_SKIP_CONFIG,
	VFE_CMD_ID_OUTPUT_CLAMP_CONFIG,

	/* test gen */
	VFE_CMD_ID_TEST_GEN_START,

	VFE_CMD_ID_UPDATE,

	/* ackownledge from upper layer */
	VFE_CMD_ID_OUTPUT1_ACK,
	VFE_CMD_ID_OUTPUT2_ACK,
	VFE_CMD_ID_EPOCH1_ACK,
	VFE_CMD_ID_EPOCH2_ACK,
	VFE_CMD_ID_STATS_AUTOFOCUS_ACK,
	VFE_CMD_ID_STATS_WB_EXP_ACK,

	/* module update commands */
	VFE_CMD_ID_BLACK_LEVEL_UPDATE,
	VFE_CMD_ID_DEMUX_CHANNEL_GAIN_UPDATE,
	VFE_CMD_ID_DEMOSAIC_BPC_UPDATE,
	VFE_CMD_ID_DEMOSAIC_ABF_UPDATE,
	VFE_CMD_ID_FOV_CROP_UPDATE,
	VFE_CMD_ID_WHITE_BALANCE_UPDATE,
	VFE_CMD_ID_COLOR_CORRECTION_UPDATE,
	VFE_CMD_ID_LA_UPDATE,
	VFE_CMD_ID_RGB_GAMMA_UPDATE,
	VFE_CMD_ID_CHROMA_ENHAN_UPDATE,
	VFE_CMD_ID_CHROMA_SUPPRESSION_UPDATE,
	VFE_CMD_ID_MAIN_SCALER_UPDATE,
	VFE_CMD_ID_SCALER2CbCr_UPDATE,
	VFE_CMD_ID_SCALER2Y_UPDATE,
	VFE_CMD_ID_ASF_UPDATE,
	VFE_CMD_ID_FRAME_SKIP_UPDATE,
	VFE_CMD_ID_CAMIF_FRAME_UPDATE,

	/* stats update commands */
	VFE_CMD_ID_STATS_AUTOFOCUS_UPDATE,
	VFE_CMD_ID_STATS_WB_EXP_UPDATE,

	/* control of start, stop, update, etc... */
  VFE_CMD_ID_STOP,
	VFE_CMD_ID_GET_HW_VERSION,

	/* stats */
	VFE_CMD_ID_STATS_SETTING,
	VFE_CMD_ID_STATS_AUTOFOCUS_START,
	VFE_CMD_ID_STATS_AUTOFOCUS_STOP,
	VFE_CMD_ID_STATS_WB_EXP_START,
	VFE_CMD_ID_STATS_WB_EXP_STOP,

	VFE_CMD_ID_ASYNC_TIMER_SETTING,

	/* max id  */
	VFE_CMD_ID_MAX
};

struct vfe_cmd_hw_version {
	uint32_t minorVersion;
	uint32_t majorVersion;
	uint32_t coreVersion;
};

enum VFE_CAMIF_SYNC_EDGE {
	VFE_CAMIF_SYNC_EDGE_ActiveHigh,
	VFE_CAMIF_SYNC_EDGE_ActiveLow
};

enum VFE_CAMIF_SYNC_MODE {
	VFE_CAMIF_SYNC_MODE_APS,
	VFE_CAMIF_SYNC_MODE_EFS,
	VFE_CAMIF_SYNC_MODE_ELS,
	VFE_CAMIF_SYNC_MODE_ILLEGAL
};

struct vfe_cmds_camif_efs {
	uint8_t efsendofline;
	uint8_t efsstartofline;
	uint8_t efsendofframe;
	uint8_t efsstartofframe;
};

struct vfe_cmds_camif_frame {
	uint16_t pixelsPerLine;
	uint16_t linesPerFrame;
};

struct vfe_cmds_camif_window {
	uint16_t firstpixel;
	uint16_t lastpixel;
	uint16_t firstline;
	uint16_t lastline;
};

enum CAMIF_SUBSAMPLE_FRAME_SKIP {
	CAMIF_SUBSAMPLE_FRAME_SKIP_0,
	CAMIF_SUBSAMPLE_FRAME_SKIP_AllFrames,
	CAMIF_SUBSAMPLE_FRAME_SKIP_ONE_OUT_OF_EVERY_2Frame,
	CAMIF_SUBSAMPLE_FRAME_SKIP_ONE_OUT_OF_EVERY_3Frame,
	CAMIF_SUBSAMPLE_FRAME_SKIP_ONE_OUT_OF_EVERY_4Frame,
	CAMIF_SUBSAMPLE_FRAME_SKIP_ONE_OUT_OF_EVERY_5Frame,
	CAMIF_SUBSAMPLE_FRAME_SKIP_ONE_OUT_OF_EVERY_6Frame,
	CAMIF_SUBSAMPLE_FRAME_SKIP_ONE_OUT_OF_EVERY_7Frame,
	CAMIF_SUBSAMPLE_FRAME_SKIP_ONE_OUT_OF_EVERY_8Frame,
	CAMIF_SUBSAMPLE_FRAME_SKIP_ONE_OUT_OF_EVERY_9Frame,
	CAMIF_SUBSAMPLE_FRAME_SKIP_ONE_OUT_OF_EVERY_10Frame,
	CAMIF_SUBSAMPLE_FRAME_SKIP_ONE_OUT_OF_EVERY_11Frame,
	CAMIF_SUBSAMPLE_FRAME_SKIP_ONE_OUT_OF_EVERY_12Frame,
	CAMIF_SUBSAMPLE_FRAME_SKIP_ONE_OUT_OF_EVERY_13Frame,
	CAMIF_SUBSAMPLE_FRAME_SKIP_ONE_OUT_OF_EVERY_14Frame,
	CAMIF_SUBSAMPLE_FRAME_SKIP_ONE_OUT_OF_EVERY_15Frame
};

struct vfe_cmds_camif_subsample {
	uint16_t pixelskipmask;
	uint16_t lineskipmask;
	enum CAMIF_SUBSAMPLE_FRAME_SKIP frameskip;
	uint8_t frameskipmode;
	uint8_t pixelskipwrap;
};

struct vfe_cmds_camif_epoch {
	uint8_t  enable;
	uint16_t lineindex;
};

struct vfe_cmds_camif_cfg {
	enum VFE_CAMIF_SYNC_EDGE  vSyncEdge;
	enum VFE_CAMIF_SYNC_EDGE  hSyncEdge;
	enum VFE_CAMIF_SYNC_MODE  syncMode;
	uint8_t vfeSubSampleEnable;
	uint8_t busSubSampleEnable;
	uint8_t irqSubSampleEnable;
	uint8_t binningEnable;
	uint8_t misrEnable;
};

struct vfe_cmd_camif_config {
	struct vfe_cmds_camif_cfg camifConfig;
	struct vfe_cmds_camif_efs EFS;
	struct vfe_cmds_camif_frame     frame;
	struct vfe_cmds_camif_window    window;
	struct vfe_cmds_camif_subsample subsample;
	struct vfe_cmds_camif_epoch     epoch1;
	struct vfe_cmds_camif_epoch     epoch2;
};

enum VFE_AXI_OUTPUT_MODE {
	VFE_AXI_OUTPUT_MODE_Output1,
	VFE_AXI_OUTPUT_MODE_Output2,
	VFE_AXI_OUTPUT_MODE_Output1AndOutput2,
	VFE_AXI_OUTPUT_MODE_CAMIFToAXIViaOutput2,
	VFE_AXI_OUTPUT_MODE_Output2AndCAMIFToAXIViaOutput1,
	VFE_AXI_OUTPUT_MODE_Output1AndCAMIFToAXIViaOutput2,
	VFE_AXI_LAST_OUTPUT_MODE_ENUM
};

enum VFE_RAW_WR_PATH_SEL {
	VFE_RAW_OUTPUT_DISABLED,
	VFE_RAW_OUTPUT_ENC_CBCR_PATH,
	VFE_RAW_OUTPUT_VIEW_CBCR_PATH,
	VFE_RAW_OUTPUT_PATH_INVALID
};

enum VFE_RAW_PIXEL_DATA_SIZE {
	VFE_RAW_PIXEL_DATA_SIZE_8BIT,
	VFE_RAW_PIXEL_DATA_SIZE_10BIT,
	VFE_RAW_PIXEL_DATA_SIZE_12BIT,
};

#define VFE_AXI_OUTPUT_BURST_LENGTH     4
#define VFE_MAX_NUM_FRAGMENTS_PER_FRAME 4
#define VFE_AXI_OUTPUT_CFG_FRAME_COUNT  3

struct vfe_cmds_axi_out_per_component {
	uint16_t imageWidth;
	uint16_t imageHeight;
	uint16_t outRowCount;
	uint16_t outRowIncrement;
	uint32_t outFragments[VFE_AXI_OUTPUT_CFG_FRAME_COUNT]
		[VFE_MAX_NUM_FRAGMENTS_PER_FRAME];
};

struct vfe_cmds_axi_per_output_path {
	uint8_t fragmentCount;
	struct vfe_cmds_axi_out_per_component outputY;
	struct vfe_cmds_axi_out_per_component outputCbcr;
};

enum VFE_AXI_BURST_LENGTH {
	VFE_AXI_BURST_LENGTH_IS_2  = 2,
	VFE_AXI_BURST_LENGTH_IS_4  = 4,
	VFE_AXI_BURST_LENGTH_IS_8  = 8,
	VFE_AXI_BURST_LENGTH_IS_16 = 16
};

struct vfe_cmd_axi_output_config {
	enum VFE_AXI_BURST_LENGTH burstLength;
	enum VFE_AXI_OUTPUT_MODE outputMode;
	enum VFE_RAW_PIXEL_DATA_SIZE outputDataSize;
	struct vfe_cmds_axi_per_output_path output1;
	struct vfe_cmds_axi_per_output_path output2;
};

struct vfe_cmd_fov_crop_config {
	uint8_t enable;
	uint16_t firstPixel;
	uint16_t lastPixel;
	uint16_t firstLine;
	uint16_t lastLine;
};

struct vfe_cmds_main_scaler_stripe_init {
	uint16_t MNCounterInit;
	uint16_t phaseInit;
};

struct vfe_cmds_scaler_one_dimension {
	uint8_t  enable;
	uint16_t inputSize;
	uint16_t outputSize;
	uint32_t phaseMultiplicationFactor;
	uint8_t  interpolationResolution;
};

struct vfe_cmd_main_scaler_config {
	uint8_t enable;
	struct vfe_cmds_scaler_one_dimension    hconfig;
	struct vfe_cmds_scaler_one_dimension    vconfig;
	struct vfe_cmds_main_scaler_stripe_init MNInitH;
	struct vfe_cmds_main_scaler_stripe_init MNInitV;
};

struct vfe_cmd_scaler2_config {
	uint8_t enable;
	struct vfe_cmds_scaler_one_dimension hconfig;
	struct vfe_cmds_scaler_one_dimension vconfig;
};

struct vfe_cmd_frame_skip_config {
	uint8_t output1Period;
	uint32_t output1Pattern;
	uint8_t output2Period;
	uint32_t output2Pattern;
};

struct vfe_cmd_frame_skip_update {
	uint32_t output1Pattern;
	uint32_t output2Pattern;
};

struct vfe_cmd_output_clamp_config {
	uint8_t minCh0;
	uint8_t minCh1;
	uint8_t minCh2;
	uint8_t maxCh0;
	uint8_t maxCh1;
	uint8_t maxCh2;
};

struct vfe_cmd_chroma_subsample_config {
	uint8_t enable;
	uint8_t cropEnable;
	uint8_t vsubSampleEnable;
	uint8_t hsubSampleEnable;
	uint8_t vCosited;
	uint8_t hCosited;
	uint8_t vCositedPhase;
	uint8_t hCositedPhase;
	uint16_t cropWidthFirstPixel;
	uint16_t cropWidthLastPixel;
	uint16_t cropHeightFirstLine;
	uint16_t cropHeightLastLine;
};

enum VFE_START_INPUT_SOURCE {
	VFE_START_INPUT_SOURCE_CAMIF,
	VFE_START_INPUT_SOURCE_TESTGEN,
	VFE_START_INPUT_SOURCE_AXI,
	VFE_START_INPUT_SOURCE_INVALID
};

enum VFE_START_OPERATION_MODE {
	VFE_START_OPERATION_MODE_CONTINUOUS,
	VFE_START_OPERATION_MODE_SNAPSHOT
};

enum VFE_START_PIXEL_PATTERN {
	VFE_BAYER_RGRGRG,
	VFE_BAYER_GRGRGR,
	VFE_BAYER_BGBGBG,
	VFE_BAYER_GBGBGB,
	VFE_YUV_YCbYCr,
	VFE_YUV_YCrYCb,
	VFE_YUV_CbYCrY,
	VFE_YUV_CrYCbY
};

enum VFE_BUS_RD_INPUT_PIXEL_PATTERN {
	VFE_BAYER_RAW,
	VFE_YUV_INTERLEAVED,
	VFE_YUV_PSEUDO_PLANAR_Y,
	VFE_YUV_PSEUDO_PLANAR_CBCR
};

enum VFE_YUV_INPUT_COSITING_MODE {
	VFE_YUV_COSITED,
	VFE_YUV_INTERPOLATED
};

struct vfe_cmd_start {
	enum VFE_START_INPUT_SOURCE inputSource;
	enum VFE_START_OPERATION_MODE operationMode;
	uint8_t     snapshotCount;
	enum VFE_START_PIXEL_PATTERN pixel;
	enum VFE_YUV_INPUT_COSITING_MODE yuvInputCositingMode;
};

struct vfe_cmd_output_ack {
	uint32_t ybufaddr[VFE_MAX_NUM_FRAGMENTS_PER_FRAME];
	uint32_t chromabufaddr[VFE_MAX_NUM_FRAGMENTS_PER_FRAME];
};

#define VFE_STATS_BUFFER_COUNT 3

struct vfe_cmd_stats_setting {
	uint16_t frameHDimension;
	uint16_t frameVDimension;
	uint8_t  afBusPrioritySelection;
	uint8_t  afBusPriority;
	uint8_t  awbBusPrioritySelection;
	uint8_t  awbBusPriority;
	uint8_t  histBusPrioritySelection;
	uint8_t  histBusPriority;
	uint32_t afBuffer[VFE_STATS_BUFFER_COUNT];
	uint32_t awbBuffer[VFE_STATS_BUFFER_COUNT];
	uint32_t histBuffer[VFE_STATS_BUFFER_COUNT];
};

struct vfe_cmd_stats_af_start {
	uint8_t  enable;
	uint8_t  windowMode;
	uint16_t windowHOffset;
	uint16_t windowVOffset;
	uint16_t windowWidth;
	uint16_t windowHeight;
	uint8_t  gridForMultiWindows[16];
	uint8_t     metricSelection;
	int16_t  metricMax;
	int8_t   highPassCoef[7];
	int8_t   bufferHeader;
};

struct vfe_cmd_stats_af_update {
	uint8_t  windowMode;
	uint16_t windowHOffset;
	uint16_t windowVOffset;
	uint16_t windowWidth;
	uint16_t windowHeight;
};

struct vfe_cmd_stats_wb_exp_start {
	uint8_t   enable;
	uint8_t   wbExpRegions;
	uint8_t   wbExpSubRegion;
	uint8_t   awbYMin;
	uint8_t   awbYMax;
	int8_t    awbMCFG[4];
	int16_t   awbCCFG[4];
	int8_t    axwHeader;
};

struct vfe_cmd_stats_wb_exp_update {
	uint8_t wbExpRegions;
	uint8_t wbExpSubRegion;
	int8_t  awbYMin;
	int8_t  awbYMax;
	int8_t  awbMCFG[4];
	int16_t awbCCFG[4];
};

struct vfe_cmd_stats_af_ack {
	uint32_t nextAFOutputBufferAddr;
};

struct vfe_cmd_stats_wb_exp_ack {
	uint32_t  nextWbExpOutputBufferAddr;
};

struct vfe_cmd_black_level_config {
	uint8_t  enable;
	uint16_t evenEvenAdjustment;
	uint16_t evenOddAdjustment;
	uint16_t oddEvenAdjustment;
	uint16_t oddOddAdjustment;
};

/* 13*1  */
#define  VFE_ROLL_OFF_INIT_TABLE_SIZE  13
/* 13*16 */
#define  VFE_ROLL_OFF_DELTA_TABLE_SIZE 208

struct vfe_cmd_roll_off_config {
	uint8_t  enable;
	uint16_t gridWidth;
	uint16_t gridHeight;
	uint16_t  yDelta;
	uint8_t  gridXIndex;
	uint8_t  gridYIndex;
	uint16_t gridPixelXIndex;
	uint16_t gridPixelYIndex;
	uint16_t yDeltaAccum;
	uint16_t initTableR[VFE_ROLL_OFF_INIT_TABLE_SIZE];
	uint16_t initTableGr[VFE_ROLL_OFF_INIT_TABLE_SIZE];
	uint16_t initTableB[VFE_ROLL_OFF_INIT_TABLE_SIZE];
	uint16_t initTableGb[VFE_ROLL_OFF_INIT_TABLE_SIZE];
	int16_t  deltaTableR[VFE_ROLL_OFF_DELTA_TABLE_SIZE];
	int16_t  deltaTableGr[VFE_ROLL_OFF_DELTA_TABLE_SIZE];
	int16_t  deltaTableB[VFE_ROLL_OFF_DELTA_TABLE_SIZE];
	int16_t  deltaTableGb[VFE_ROLL_OFF_DELTA_TABLE_SIZE];
};

struct vfe_cmd_demux_channel_gain_config {
	uint16_t ch0EvenGain;
	uint16_t ch0OddGain;
	uint16_t ch1Gain;
	uint16_t ch2Gain;
};

struct vfe_cmds_demosaic_abf {
	uint8_t   enable;
	uint8_t   forceOn;
	uint8_t   shift;
	uint16_t  lpThreshold;
	uint16_t  max;
	uint16_t  min;
	uint8_t   ratio;
};

struct vfe_cmds_demosaic_bpc {
	uint8_t   enable;
	uint16_t  fmaxThreshold;
	uint16_t  fminThreshold;
	uint16_t  redDiffThreshold;
	uint16_t  blueDiffThreshold;
	uint16_t  greenDiffThreshold;
};

struct vfe_cmd_demosaic_config {
	uint8_t   enable;
	uint8_t   slopeShift;
	struct vfe_cmds_demosaic_abf abfConfig;
	struct vfe_cmds_demosaic_bpc bpcConfig;
};

struct vfe_cmd_demosaic_bpc_update {
	struct vfe_cmds_demosaic_bpc bpcUpdate;
};

struct vfe_cmd_demosaic_abf_update {
	struct vfe_cmds_demosaic_abf abfUpdate;
};

struct vfe_cmd_white_balance_config {
	uint8_t  enable;
	uint16_t ch2Gain;
	uint16_t ch1Gain;
	uint16_t ch0Gain;
};

enum VFE_COLOR_CORRECTION_COEF_QFACTOR {
	COEF_IS_Q7_SIGNED,
	COEF_IS_Q8_SIGNED,
	COEF_IS_Q9_SIGNED,
	COEF_IS_Q10_SIGNED
};

struct vfe_cmd_color_correction_config {
	uint8_t     enable;
	enum VFE_COLOR_CORRECTION_COEF_QFACTOR coefQFactor;
	int16_t  C0;
	int16_t  C1;
	int16_t  C2;
	int16_t  C3;
	int16_t  C4;
	int16_t  C5;
	int16_t  C6;
	int16_t  C7;
	int16_t  C8;
	int16_t  K0;
	int16_t  K1;
	int16_t  K2;
};

#define VFE_LA_TABLE_LENGTH 256
struct vfe_cmd_la_config {
	uint8_t enable;
	int16_t table[VFE_LA_TABLE_LENGTH];
};

#define VFE_GAMMA_TABLE_LENGTH 256
enum VFE_RGB_GAMMA_TABLE_SELECT {
	RGB_GAMMA_CH0_SELECTED,
	RGB_GAMMA_CH1_SELECTED,
	RGB_GAMMA_CH2_SELECTED,
	RGB_GAMMA_CH0_CH1_SELECTED,
	RGB_GAMMA_CH0_CH2_SELECTED,
	RGB_GAMMA_CH1_CH2_SELECTED,
	RGB_GAMMA_CH0_CH1_CH2_SELECTED
};

struct vfe_cmd_rgb_gamma_config {
	uint8_t enable;
	enum VFE_RGB_GAMMA_TABLE_SELECT channelSelect;
	int16_t table[VFE_GAMMA_TABLE_LENGTH];
};

struct vfe_cmd_chroma_enhan_config {
	uint8_t  enable;
	int16_t am;
	int16_t ap;
	int16_t bm;
	int16_t bp;
	int16_t cm;
	int16_t cp;
	int16_t dm;
	int16_t dp;
	int16_t kcr;
	int16_t kcb;
	int16_t RGBtoYConversionV0;
	int16_t RGBtoYConversionV1;
	int16_t RGBtoYConversionV2;
	uint8_t RGBtoYConversionOffset;
};

struct vfe_cmd_chroma_suppression_config {
	uint8_t enable;
	uint8_t m1;
	uint8_t m3;
	uint8_t n1;
	uint8_t n3;
	uint8_t nn1;
	uint8_t mm1;
};

struct vfe_cmd_asf_config {
	uint8_t enable;
	uint8_t smoothFilterEnabled;
	uint8_t sharpMode;
	uint8_t smoothCoefCenter;
	uint8_t smoothCoefSurr;
	uint8_t normalizeFactor;
	uint8_t sharpK1;
	uint8_t sharpK2;
	uint8_t sharpThreshE1;
	int8_t sharpThreshE2;
	int8_t sharpThreshE3;
	int8_t sharpThreshE4;
	int8_t sharpThreshE5;
	int8_t filter1Coefficients[9];
	int8_t filter2Coefficients[9];
	uint8_t  cropEnable;
	uint16_t cropFirstPixel;
	uint16_t cropLastPixel;
	uint16_t cropFirstLine;
	uint16_t cropLastLine;
};

struct vfe_cmd_asf_update {
	uint8_t enable;
	uint8_t smoothFilterEnabled;
	uint8_t sharpMode;
	uint8_t smoothCoefCenter;
	uint8_t smoothCoefSurr;
	uint8_t normalizeFactor;
	uint8_t sharpK1;
	uint8_t sharpK2;
	uint8_t sharpThreshE1;
	int8_t  sharpThreshE2;
	int8_t  sharpThreshE3;
	int8_t  sharpThreshE4;
	int8_t  sharpThreshE5;
	int8_t  filter1Coefficients[9];
	int8_t  filter2Coefficients[9];
	uint8_t cropEnable;
};

enum VFE_TEST_GEN_SYNC_EDGE {
	VFE_TEST_GEN_SYNC_EDGE_ActiveHigh,
	VFE_TEST_GEN_SYNC_EDGE_ActiveLow
};

struct vfe_cmd_test_gen_start {
	uint8_t pixelDataSelect;
	uint8_t systematicDataSelect;
	enum VFE_TEST_GEN_SYNC_EDGE  hsyncEdge;
	enum VFE_TEST_GEN_SYNC_EDGE  vsyncEdge;
	uint16_t numFrame;
	enum VFE_RAW_PIXEL_DATA_SIZE pixelDataSize;
	uint16_t imageWidth;
	uint16_t imageHeight;
	uint32_t startOfFrameOffset;
	uint32_t endOfFrameNOffset;
	uint16_t startOfLineOffset;
	uint16_t endOfLineNOffset;
	uint16_t hbi;
	uint8_t  vblEnable;
	uint16_t vbl;
	uint8_t  startOfFrameDummyLine;
	uint8_t  endOfFrameDummyLine;
	uint8_t  unicolorBarEnable;
	uint8_t  colorBarsSplitEnable;
	uint8_t  unicolorBarSelect;
	enum VFE_START_PIXEL_PATTERN  colorBarsPixelPattern;
	uint8_t  colorBarsRotatePeriod;
	uint16_t testGenRandomSeed;
};

struct vfe_cmd_bus_pm_start {
	uint8_t output2YWrPmEnable;
	uint8_t output2CbcrWrPmEnable;
	uint8_t output1YWrPmEnable;
	uint8_t output1CbcrWrPmEnable;
};

struct vfe_cmd_camif_frame_update {
	struct vfe_cmds_camif_frame camifFrame;
};

struct vfe_cmd_sync_timer_setting {
	uint8_t  whichSyncTimer;
	uint8_t  operation;
	uint8_t  polarity;
	uint16_t repeatCount;
	uint16_t hsyncCount;
	uint32_t pclkCount;
	uint32_t outputDuration;
};

struct vfe_cmd_async_timer_setting {
	uint8_t  whichAsyncTimer;
	uint8_t  operation;
	uint8_t  polarity;
	uint16_t repeatCount;
	uint16_t inactiveCount;
	uint32_t activeCount;
};

struct  vfe_frame_skip_counts {
	uint32_t  totalFrameCount;
	uint32_t  output1Count;
	uint32_t  output2Count;
};

enum VFE_AXI_RD_UNPACK_HBI_SEL {
	VFE_AXI_RD_HBI_32_CLOCK_CYCLES,
	VFE_AXI_RD_HBI_64_CLOCK_CYCLES,
	VFE_AXI_RD_HBI_128_CLOCK_CYCLES,
	VFE_AXI_RD_HBI_256_CLOCK_CYCLES,
	VFE_AXI_RD_HBI_512_CLOCK_CYCLES,
	VFE_AXI_RD_HBI_1024_CLOCK_CYCLES,
	VFE_AXI_RD_HBI_2048_CLOCK_CYCLES,
	VFE_AXI_RD_HBI_4096_CLOCK_CYCLES
};

struct vfe_cmd_axi_input_config {
	uint32_t  fragAddr[4];
	uint8_t   totalFragmentCount;
	uint16_t  ySize;
	uint16_t  xOffset;
	uint16_t  xSize;
	uint16_t  rowIncrement;
	uint16_t  numOfRows;
	enum VFE_AXI_BURST_LENGTH burstLength;
	uint8_t   unpackPhase;
	enum VFE_AXI_RD_UNPACK_HBI_SEL unpackHbi;
	enum VFE_RAW_PIXEL_DATA_SIZE   pixelSize;
	uint8_t   padRepeatCountLeft;
	uint8_t   padRepeatCountRight;
	uint8_t   padRepeatCountTop;
	uint8_t   padRepeatCountBottom;
	uint8_t   padLeftComponentSelectCycle0;
	uint8_t   padLeftComponentSelectCycle1;
	uint8_t   padLeftComponentSelectCycle2;
	uint8_t   padLeftComponentSelectCycle3;
	uint8_t   padLeftStopCycle0;
	uint8_t   padLeftStopCycle1;
	uint8_t   padLeftStopCycle2;
	uint8_t   padLeftStopCycle3;
	uint8_t   padRightComponentSelectCycle0;
	uint8_t   padRightComponentSelectCycle1;
	uint8_t   padRightComponentSelectCycle2;
	uint8_t   padRightComponentSelectCycle3;
	uint8_t   padRightStopCycle0;
	uint8_t   padRightStopCycle1;
	uint8_t   padRightStopCycle2;
	uint8_t   padRightStopCycle3;
	uint8_t   padTopLineCount;
	uint8_t   padBottomLineCount;
};

struct vfe_interrupt_status {
	uint8_t camifErrorIrq;
	uint8_t camifSofIrq;
	uint8_t camifEolIrq;
	uint8_t camifEofIrq;
	uint8_t camifEpoch1Irq;
	uint8_t camifEpoch2Irq;
	uint8_t camifOverflowIrq;
	uint8_t ceIrq;
	uint8_t regUpdateIrq;
	uint8_t resetAckIrq;
	uint8_t encYPingpongIrq;
	uint8_t encCbcrPingpongIrq;
	uint8_t viewYPingpongIrq;
	uint8_t viewCbcrPingpongIrq;
	uint8_t rdPingpongIrq;
	uint8_t afPingpongIrq;
	uint8_t awbPingpongIrq;
	uint8_t histPingpongIrq;
	uint8_t encIrq;
	uint8_t viewIrq;
	uint8_t busOverflowIrq;
	uint8_t afOverflowIrq;
	uint8_t awbOverflowIrq;
	uint8_t syncTimer0Irq;
	uint8_t syncTimer1Irq;
	uint8_t syncTimer2Irq;
	uint8_t asyncTimer0Irq;
	uint8_t asyncTimer1Irq;
	uint8_t asyncTimer2Irq;
	uint8_t asyncTimer3Irq;
	uint8_t axiErrorIrq;
	uint8_t violationIrq;
	uint8_t anyErrorIrqs;
	uint8_t anyOutput1PathIrqs;
	uint8_t anyOutput2PathIrqs;
	uint8_t anyOutputPathIrqs;
	uint8_t anyAsyncTimerIrqs;
	uint8_t anySyncTimerIrqs;
	uint8_t anyIrqForActiveStatesOnly;
};

enum VFE_MESSAGE_ID {
	VFE_MSG_ID_RESET_ACK,
	VFE_MSG_ID_START_ACK,
	VFE_MSG_ID_STOP_ACK,
	VFE_MSG_ID_UPDATE_ACK,
	VFE_MSG_ID_OUTPUT1,
	VFE_MSG_ID_OUTPUT2,
	VFE_MSG_ID_SNAPSHOT_DONE,
	VFE_MSG_ID_STATS_AUTOFOCUS,
	VFE_MSG_ID_STATS_WB_EXP,
	VFE_MSG_ID_EPOCH1,
	VFE_MSG_ID_EPOCH2,
	VFE_MSG_ID_SYNC_TIMER0_DONE,
	VFE_MSG_ID_SYNC_TIMER1_DONE,
	VFE_MSG_ID_SYNC_TIMER2_DONE,
	VFE_MSG_ID_ASYNC_TIMER0_DONE,
	VFE_MSG_ID_ASYNC_TIMER1_DONE,
	VFE_MSG_ID_ASYNC_TIMER2_DONE,
	VFE_MSG_ID_ASYNC_TIMER3_DONE,
	VFE_MSG_ID_AF_OVERFLOW,
	VFE_MSG_ID_AWB_OVERFLOW,
	VFE_MSG_ID_AXI_ERROR,
	VFE_MSG_ID_CAMIF_OVERFLOW,
	VFE_MSG_ID_VIOLATION,
	VFE_MSG_ID_CAMIF_ERROR,
	VFE_MSG_ID_BUS_OVERFLOW,
};

struct vfe_msg_stats_autofocus {
	uint32_t    afBuffer;
	uint32_t    frameCounter;
};

struct vfe_msg_stats_wb_exp {
	uint32_t awbBuffer;
	uint32_t frameCounter;
};

struct vfe_frame_bpc_info {
	uint32_t greenDefectPixelCount;
	uint32_t redBlueDefectPixelCount;
};

struct vfe_frame_asf_info {
	uint32_t  asfMaxEdge;
	uint32_t  asfHbiCount;
};

struct vfe_msg_camif_status {
	uint8_t  camifState;
	uint32_t pixelCount;
	uint32_t lineCount;
};

struct vfe_bus_pm_per_path {
	uint32_t yWrPmStats0;
	uint32_t yWrPmStats1;
	uint32_t cbcrWrPmStats0;
	uint32_t cbcrWrPmStats1;
};

struct vfe_bus_performance_monitor {
	struct vfe_bus_pm_per_path encPathPmInfo;
	struct vfe_bus_pm_per_path viewPathPmInfo;
};

struct vfe_irq_thread_msg {
	uint32_t  vfeIrqStatus;
	uint32_t  camifStatus;
	uint32_t  demosaicStatus;
	uint32_t  asfMaxEdge;
	struct vfe_bus_performance_monitor pmInfo;
};

struct vfe_msg_output {
	uint32_t  yBuffer;
	uint32_t  cbcrBuffer;
	struct vfe_frame_bpc_info bpcInfo;
	struct vfe_frame_asf_info asfInfo;
	uint32_t  frameCounter;
	struct vfe_bus_pm_per_path pmData;
};

struct vfe_message {
	enum VFE_MESSAGE_ID _d;
	union {
		struct vfe_msg_output              msgOutput1;
		struct vfe_msg_output              msgOutput2;
		struct vfe_msg_stats_autofocus     msgStatsAf;
		struct vfe_msg_stats_wb_exp        msgStatsWbExp;
		struct vfe_msg_camif_status        msgCamifError;
		struct vfe_bus_performance_monitor msgBusOverflow;
   } _u;
};

/* New one for 8k */
struct msm_vfe_command_8k {
	int32_t  id;
	uint16_t length;
	void     *value;
};

struct vfe_frame_extra {
	struct vfe_frame_bpc_info bpcInfo;
	struct vfe_frame_asf_info asfInfo;
	uint32_t  frameCounter;
	struct vfe_bus_pm_per_path pmData;
};
#endif /* __MSM_VFE8X_H__ */

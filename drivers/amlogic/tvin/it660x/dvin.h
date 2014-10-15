#ifndef DVIN_H
#define DVIN_H

#define DEBUG_DVIN

#ifdef DEBUG_DVIN  
extern int hs_pol_inv;           
extern int vs_pol_inv;           
extern int de_pol_inv;           
extern int field_pol_inv;        
extern int ext_field_sel;        
extern int de_mode;              
extern int data_comp_map;        
extern int mode_422to444;        
extern int dvin_clk_inv;         
extern int vs_hs_tim_ctrl;       
extern int hs_lead_vs_odd_min;   
extern int hs_lead_vs_odd_max;   
extern int active_start_pix_fe;  
extern int active_start_pix_fo;  
extern int active_start_line_fe; 
extern int active_start_line_fo; 
extern int line_width;           
extern int field_height; 
#endif       

extern void config_dvin (unsigned long hs_pol_inv,             // Invert HS polarity, for HW regards HS active high.
                         unsigned long vs_pol_inv,             // Invert VS polarity, for HW regards VS active high.
                         unsigned long de_pol_inv,             // Invert DE polarity, for HW regards DE active high.
                         unsigned long field_pol_inv,          // Invert FIELD polarity, for HW regards odd field when high.
                         unsigned long ext_field_sel,          // FIELD source select:
                                                               // 1=Use external FIELD signal, ignore internal FIELD detection result;
                                                               // 0=Use internal FIELD detection result, ignore external input FIELD signal.
                         unsigned long de_mode,                // DE mode control:
                                                               // 0=Ignore input DE signal, use internal detection to to determine active pixel;
                                                               // 1=Rsrv;
                                                               // 2=During internal detected active region, if input DE goes low, replace input data with the last good data;
                                                               // 3=Active region is determined by input DE, no internal detection.
                         unsigned long data_comp_map,          // Map input data to form YCbCr.
                                                               // Use 0 if input is YCbCr;
                                                               // Use 1 if input is YCrCb;
                                                               // Use 2 if input is CbCrY;
                                                               // Use 3 if input is CbYCr;
                                                               // Use 4 if input is CrYCb;
                                                               // Use 5 if input is CrCbY;
                                                               // 6,7=Rsrv.
                         unsigned long mode_422to444,          // 422 to 444 conversion control:
                                                               // 0=No convertion; 1=Rsrv;
                                                               // 2=Convert 422 to 444, use previous C value;
                                                               // 3=Convert 422 to 444, use average C value.
                         unsigned long dvin_clk_inv,           // Invert dvin_clk_in for ease of data capture.
                         unsigned long vs_hs_tim_ctrl,         // Controls which edge of HS/VS (post polarity control) the active pixel/line is related:
                                                               // Bit 0: HS and active pixel relation.
                                                               //  0=Start of active pixel is counted from the rising edge of HS;
                                                               //  1=Start of active pixel is counted from the falling edge of HS;
                                                               // Bit 1: VS and active line relation.
                                                               //  0=Start of active line is counted from the rising edge of VS;
                                                               //  1=Start of active line is counted from the falling edge of VS.
                         unsigned long hs_lead_vs_odd_min,     // For internal FIELD detection:
                                                               // Minimum clock cycles allowed for HS active edge to lead before VS active edge in odd field. Failing it the field is even.
                         unsigned long hs_lead_vs_odd_max,     // For internal FIELD detection:
                                                               // Maximum clock cycles allowed for HS active edge to lead before VS active edge in odd field. Failing it the field is even.
                         unsigned long active_start_pix_fe,    // Number of clock cycles between HS active edge to first active pixel, in even field.
                         unsigned long active_start_pix_fo,    // Number of clock cycles between HS active edge to first active pixel, in odd field.
                         unsigned long active_start_line_fe,   // Number of clock cycles between VS active edge to first active line, in even field.
                         unsigned long active_start_line_fo,   // Number of clock cycles between VS active edge to first active line, in odd field.
                         unsigned long line_width,             // Number_of_pixels_per_line
                         unsigned long field_height);          // Number_of_lines_per_field

                                              
#endif /*DVIN_H*/

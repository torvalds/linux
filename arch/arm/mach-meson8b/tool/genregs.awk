#!/usr/bin/awk -f

BEGIN{
	
	
	
}
/^\/\/ Secure APB3 Slot 0 Registers/{
    sec_bus="SECBUS_REG_ADDR"
}
/^\/\/ Secure APB3 Slot 2 Registers/{
    sec_bus="SECBUS2_REG_ADDR"
}
/^\/\/ Secure APB3 Slot 3 registers/{
    sec_bus="SECBUS3_REG_ADDR"
}

/^#define/ && $2 ~/VDIN0_OFFSET/{
	VDIN0_OFFSET = $3
}
/^#define/ && $2 ~/VDIN1_OFFSET/{
	VDIN1_OFFSET = $3
}

#/^#define/ && $2 ~/^P_AO_RTC_/{
#    print $1,$2,"\t\tSECBUS_REG_ADDR(" $8,$9,$10,"\t///" FILENAME
#    ignore RTC
#    next
#}

########################### 3 ##########################
/^#define/ && NF==3{
    BASE="CBUS_REG_ADDR";
    REGNAME=$2
    ADDR=$3
    
#register.h    
#		if(index(FILENAME,"register")!=0){
#			if(REGNAME ~ /^VDIN/){
#				print $1, $2, $3
#			}
#		# ignore 3 paremeters define in register.h
#				next    
#dos_cbus_register.h        	
#    }else 
#    if(index(FILENAME,"dos_cbus_register")!=0){
#        BASE="DOS_REG_ADDR";
#secure_apb.h
#    }else 
    	if(index(FILENAME,"secure_apb")!=0){
        BASE=sec_bus;
        if($2 ~/^AO_RTC_/)
        	next
#c_stb_define.h
    }else if(index(FILENAME,"c_stb_define")!=0){
        BASE="CBUS_REG_ADDR";
#ddr3_reg.h
    }else if(index(FILENAME,"ddr3_reg")!=0) {
    		ADDR="0x"substr(ADDR,7,10);
        BASE="MMC_REG_ADDR";
        if(REGNAME ~ /^P_/)
        	REGNAME=substr(REGNAME,3,99);
#dmc_reg.h
    }else if(index(FILENAME,"dmc_reg")!=0) {
    		ADDR="0x"substr(ADDR,7,10);
    	print $1,"S_" REGNAME,ADDR
        next
#hdmi.h
    }else if(index(FILENAME,"hdmi")!=0){
	ADDR="0x"substr(ADDR,6,10);
        BASE="APB_REG_ADDR";

	}else{
				next
		}
    
  if(index(FILENAME,"hdmi")!=0 && $2 ~ /^STIMULUS_/)
        next;
        
	print $1,REGNAME,ADDR,"\t///"  FILENAME ":" FNR
	print $1,"P_" REGNAME , "\t\t" BASE "(" REGNAME ")"
}

########################### 7 ##########################
/^#define/ && NF==7{
	  REGNAME=$2
    ADDR=$3

#dos_register.h
		if(index(FILENAME,"dos_register")!=0) {
#  #define HCODEC_MPSR                                ((0x1301  << 2) + 0xd0050000)
    		ADDR="0x"substr(ADDR,5,8);
        BASE="DOS_REG_ADDR";
        print $1,REGNAME, ADDR,"\t///" FILENAME ":" FNR
		
#register.h
    }else if(index(FILENAME,"register")!=0){
				ADDR=substr(ADDR,3,8);
				if(ADDR ~ /^0x/ && $7 ~ /^0xc11/)
        	BASE="CBUS_REG_ADDR";
        else if(ADDR ~ /^0x/ && $7 ~ /^0xd01/)
        	BASE="VCBUS_REG_ADDR";
        else
        	next
					
				print $1,REGNAME,ADDR,"\t///" FILENAME ":" FNR
#m6tv_mmc.h
    }else if(index(FILENAME,"ddr3_reg")!=0) {
#  #define P_DDR0_PUB_DX7BDLR4         0xc8001000 + (0x18B << 2) 
				if(!(ADDR ~ /^0x/))
					next
    		ADDR="0x"substr(ADDR,7,10);
        BASE="MMC_REG_ADDR";
        if(REGNAME ~ /^P_/)
        	REGNAME=substr(REGNAME,3,99);
        	
        print $1,REGNAME,"("ADDR,$4,$5,$6,$7")","\t///" FILENAME ":" FNR
#dmc_reg.h
    }else if(index(FILENAME,"dmc_reg")!=0) {
#  #define P_DMC_DDR_CTRL         DMC_REG_BASE + (0x10  << 2)
				if(!(ADDR ~ /^DMC_REG_BASE/))
					next
    	ADDR="S_DMC_REG_BASE";
        BASE="MMC_REG_ADDR";
        if(REGNAME ~ /^P_/)
        	REGNAME=substr(REGNAME,3,99);
        	
        print $1,REGNAME,"("ADDR,$4,$5,$6,$7")","\t///" FILENAME ":" FNR
    }else 
    	next
    
		print $1,"P_" REGNAME , "\t\t" BASE "(" REGNAME ")" 

}

########################### 8 ##########################
/^#define/ && NF==8{
	  REGNAME=$2
    ADDR=$3
				
#m6tv_mmc.h
    if(index(FILENAME,"ddr3_reg")!=0) {
#  #define P_MMC_CHAN_CTRL4            0xc8006000 + (0x7b << 2 ) 
    		ADDR="0x"substr(ADDR,7,10);
        BASE="MMC_REG_ADDR";
        if(REGNAME ~ /^P_/)
        	REGNAME=substr(REGNAME,3,99);
        	
        print $1,REGNAME, "(" ADDR,$4,$5,$6,$7,$8")","\t///" FILENAME ":" FNR
        
    }else if (index(FILENAME,"register")!=0) {
##define VDIN0_WR_CTRL                           ((VDIN0_OFFSET << 2) + VDIN_WR_CTRL                      )    	
 				if(ADDR ~ /^\(\(VDIN0/){
 					VD_OFF=VDIN0_OFFSET;
 				}else if(ADDR ~ /^\(\(VDIN1/){
 					VD_OFF=VDIN1_OFFSET;
 				}else
 					next
 				
 				print $1,REGNAME, "((" VD_OFF,$4,$5,$6,$7,$8")","\t///" FILENAME ":" FNR
    }else 
    	next

		print $1,"P_" REGNAME , "\t\t" BASE "(" REGNAME ")" 

}

########################### 14 ##########################
/^#define/ && NF==14 && $2 ~ /^P_/{
# #define P_AO_RTI_PWR_CNTL_REG1      (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x03 << 2))
	  REGNAME=$2

    if(index(FILENAME,"c_always_on_pointer")!=0) {
    	BASE="AOBUS_REG_ADDR";
    	REGNAME=substr(REGNAME,3,99);
			print $1,REGNAME, "("$8,$9,$10,$11,$12,$13,$14,"\t///" FILENAME ":" FNR
    	print $1,"P_" REGNAME,"\t\t" BASE "(" REGNAME ")"  
   }

}
END{
	
}

#!/usr/bin/gawk -f
BEGIN{
	pad_index=0;
	sig_index=0;
	FS=",";
	ret=0;
	if(length(head)==0)
		headfile="cat 1>include/mach/gpio_data.h";
	else
		headfile="cat 1>" head;
	warning_on=0;
	print "/* this file is automatic generate . Please do not edit it"
	print "       ./genpintab.awk gpio_pinmux.csv > <this file name> can generate this file*/"
    print "#define AOBUS_REG_ADDR_MASK(a)   AOBUS_REG_ADDR(((a)&0xffff))"
}
#function conflict(name,value)
#{
#	if(length(table[name])==0)
#	{
#		table[name]=value;
#		return 0;
#	}
#	if( table[name]!=value)
#	{
#		print "POINT D error found conflict name=" name " value=" value | "cat 1>&2" ;
#		print "origin value=" table[name] " NR=" $2| "cat 1>&2" ;
#		return 1;
#	}
#	return 0;
#	
#}
/^TITLE/{
	for(j=1;j<=NF;j++){
		n=split($j,a," ");
		if(n==0)
			continue;
		column[j]=a[1];
		for(i=2;i<=n;i++)
		{
			column[i]= column[i] " " a[i];
		}
	}
}
/^MODULE/{
	for(j=1;j<=NF;j++){
		n=split($j,a," ");
		if(n==0)
			continue;
		module[j]=a[1];
		for(i=2;i<=n;i++)
		{
			module[i]= module[i] "_" a[i];
		}
	}
}
/^REG_ADDR/{
	for(i=1;i<=NF;i++)
	{
		str=$i;
		delete a;
		n=split(str,a," ");
		if(n!=2&&n!=1)
			continue;
		if(n==2){
			reg_addr[a[1]]=a[2];
			reg_addr_isnum[a[1]]=0;
			print "#define " a[1] " (" a[2] ")"
		}
		else{
			reg_addr[a[1]]=a[1];
			reg_addr_isnum[a[1]]=1;
		}
	
	}
}
/^ALIAS/{
	k=0;
	for(i=1;i<=NF;i++)
	{
		str=$i;
		delete a;
		n=split(str,a," ");
		if(n==4 || n==5)
		{
			if(length(idx[a[2]])==0)
			{
				
				idx[a[2]]=0;
			}
		}
		else
			continue;
		if(n == 4)
		{
			reg_list[a[2],idx[a[2]]]=a[2]"_" a[3] ;
			
			reg_alias[a[1]]=a[2] "(" (idx[a[2]]);
				
			print "#define " reg_list[a[2],idx[a[2]]] " " a[4] "(" a[1] ")"
			idx[a[2]]++;
		}else if (n == 5){
			
			for(j=1;j<=strtonum(a[4]);j++)
			{
				if(reg_addr_isnum[a[1]])
					ind=sprintf("0x%x",strtonum(a[1])+(j-1)*strtonum(a[3]));
				else{
					ind=sprintf("%s%d",a[1],(j-1));
					print "#define " ind " (" a[1] "+" (j-1)")"
				}
				reg_list[a[2],idx[a[2]]]=a[2]"_" (j-1) ;
				
#				print reg_list[a[2],0]
				reg_alias[ind]=a[2] "(" (idx[a[2]]);
				
				print "#define " reg_list[a[2],idx[a[2]]] " " a[5] "(" ind ")"
				idx[a[2]]++;
			}
			
		}else{
			continue;
		}
	}
}


/^PAD/{
	row[NR]=$2;
	if(length(pads[$2])!=0)
	{
		print "found pad name conflict name=" $2 "line=" NR| "cat 1>&2" ;
		print "origin value=" pads[$2] | "cat 1>&2" ;
		exit 1
	}
	pads[$2]=pad_index++;
	for(i=3;i<=5;i++){
		n=length($i);
		if(n==0)
			continue;
		delete a;
		str=$i;
		n=split(str,a," ");
		if(n!=2)
		{
			print "POINT A error found at NR=" NR "NF=" $i | "cat 1>&2" ;
			exit 1
		}
		if(length(reg_alias[a[1]])==0)
		{
			print "use original name for reg=_" a[1] | "cat 1>&2" 
			reg_alias[a[1]]="_" a[1];
		}
		if(match(a[2],/[\(|\[]([[:digit:]]+)[\)|\]]/,array) == 0)
		{
			print "POINT B error found at pad=" $2 " column=" column[i] " value=" $i | "cat 1>&2" ;
			exit 1
		}
		pads_gpio[$2,column[i]] = reg_alias[a[1]] "," array[1] ")";
	}
	str="";
	for(i=6;i<=NF;i++)
	{
		n=length($i);
		if(n==0)
			continue;
		delete a;
		n=split($i,a," ");
		sig=null;
		
		for(j=1;j<=n;j++){
			delete temp_arr;
			enable="xxx";
			n_reg=0;
			if(a[j] ~ /^N_([[:alpha:]_][[:alnum:]_]*)[\(|\[]([[:digit:]]+)[\)|\]]$/)
			{
				str=substr(a[j],3);
				enable="disable";
			}else if( a[j] ~ /^[a-zA-Z0-9_]*(\/)[[:digit:]]$/ )
			{
				
				sig=a[j];
				if(length(pads_pinmux[$2,sig,"enable"]) != 0 ||length(pads_pinmux[$2,sig,"disable"])!= 0)
				{
					if (length(module[i])!=0)
					{
						if(length(pads_pinmux[$2,sig,"enable"]) != 0)
							enable="enable";
						else
							enable="disable";
						
						if(warning_on>=1)print "Waring found, pads conflict " | "cat 1>&2" 
						if(warning_on>=1)print "\tsig=" sig " pads_pinmux[" $2 "," sig "," enable "]="  pads_pinmux[$2,sig,enable] | "cat 1>&2" 
						sig=a[j] "_" module[i];
						if(warning_on>=1)print "\treplace name is  " sig | "cat 1>&2" 
					}
					else {
						if(length(pads_pinmux[$2,sig,"enable"]) != 0)
							enable="enable";
						else
							enable="disable";
						print "error found, pads conflict " | "cat 1>&2" 
						print "\tsig=" sig " pads_pinmux[" $2 "," sig "," enable "]="  pads_pinmux[$2,sig,enable] | "cat 1>&2" 
						ret=1;
						break;
					}
				}
				continue;
			}else if(a[j] ~ /([[:alpha:]_][[:alnum:]_]*)[\(|\[]([[:digit:]]+)[\)|\]]$/) 
			{
				str=a[j];
				enable="enable";
			}else if( a[j] ~ /^[a-zA-Z_][a-zA-Z0-9_]*$/ )
			{
				
				sig=a[j];
				if(length(pads_pinmux[$2,sig,"enable"]) != 0 ||length(pads_pinmux[$2,sig,"disable"])!= 0)
				{
					if (length(module[i])!=0)
					{
						if(length(pads_pinmux[$2,sig,"enable"]) != 0)
							enable="enable";
						else
							enable="disable";
						
						if(warning_on>=1)print "Waring found, pads conflict " | "cat 1>&2" 
						if(warning_on>=1)print "\tsig=" sig " pads_pinmux[" $2 "," sig "," enable "]="  pads_pinmux[$2,sig,enable] | "cat 1>&2" 
						sig=a[j] "_" module[i];
						if(warning_on>=1)print "\treplace name is  " sig | "cat 1>&2" 
					}
					else {
						if(length(pads_pinmux[$2,sig,"enable"]) != 0)
							enable="enable";
						else
							enable="disable";
						print "error found, pads conflict " | "cat 1>&2" 
						print "\tsig=" sig " pads_pinmux[" $2 "," sig "," enable "]="  pads_pinmux[$2,sig,enable] | "cat 1>&2" 
						ret=1;
						break;
					}
				}
				continue;
			}
			n_reg=match(str,/([[:alpha:]_][[:alnum:]_]*)[\(|\[]([[:digit:]]+)[\)|\]]$/,temp_arr);
			if( length(sig)==0 || n_reg==0 || length(temp_arr[2])==0)
			{
				print "++++error found , wrong format at pad=" $2 " sig=" sig "," column[i] " value=" $i "," i "," n_reg "," temp_arr[2] | "cat 1>&2" ;
				ret=1;
				break;
			}
			if( length(pads_pinmux[$2,sig,enable] ) != 0 )
			{
				print "fatal error found, pads conflict " | "cat 1>&2" 
				print "\tsig=" sig " pads_pinmux[" $2 "," sig "," enable "]="  pads_pinmux[$2,sig,enable] | "cat 1>&2" 
				ret=1;
				break;
			}
			if(length(reg_alias[temp_arr[1]])==0){
				str=temp_arr[1] "(" temp_arr[2] ")" ;
				reg_alias[temp_arr[1]]=temp_arr[1] ;
				print "use original name for reg=" temp_arr[1] | "cat 1>&2" 
			}
			else
				str=reg_alias[temp_arr[1]] "," temp_arr[2] ")" ;
			
			pads_pinmux[$2,sig,enable]=str;
			if(sig in sigs)
				continue;
			sigs[sig]=sig_index++;
		}
	}
}



END{
	for(i in idx)
	{
		print "#define " i "(base,bit) (bit+(base<<5))"
		print "#define " i "_NUM " "(sizeof(" tolower(i) "_addr)/sizeof(" tolower(i) "_addr[0]))"
		print "static unsigned " tolower(i) "_addr[]={"
		for(j=0;j<idx[i];j++)
			print  "\t" reg_list[i,j] ","
		print "};"
	}
	print "#ifndef __MACH_HEAD_GPIO_DATA__" | headfile
	print "#define __MACH_HEAD_GPIO_DATA__" | headfile
	print "typedef enum {" | headfile
	x=0;
	for(i in pads)
	{
		if(x!=0)
			printf ",\n" | headfile; 
		printf ("\tPAD_%s=%d",i,pads[i]) | headfile;
		if(i in sigs){
			print "pad name " i " conflict" | "cat 1>&2" ;
			exit 1;
		}
		x++;
	}
	
	print ",\n\tPAD_MAX_PADS="  x  "\n}pad_t;" | headfile
	
	print "typedef enum {" | headfile
	x=0;
	for(i in sigs)
	{
		if(x!=0)
			printf (",\n") | headfile;
		printf ("\tSIG_%s=%d",i,sigs[i]) | headfile;
		if(i in pads){
			print "sig name " i " conflict" | "cat 1>&2" ;
			exit 1;
		}
		x++;
	}
	print ",\n\tSIG_GPIOIN=" x++  | headfile
    print ",\n\tSIG_GPIOOUT=" x++ "," | headfile
	print "\tSIG_MAX_SIGS="  x "\n}sig_t;" | headfile
	print "#endif /*__MACH_HEAD_GPIO_DATA__*/" | headfile
	print "#define NOT_EXIST -1"
	print "struct pad_sig {pad_t pad;sig_t sig;unsigned enable; unsigned disable;};"
	
	print "#define foreach_pad_sig_start(pad,sig) {int __i;for(__i=0;__i<sizeof(pad_sig_tab)/sizeof(pad_sig_tab[0]);__i++){ unsigned __pad=pad,__sig=sig;  "
	print "#define case_pad_equal(enable,disable) if(pad_sig_tab[__i].pad==__pad&&pad_sig_tab[__i].sig!=__sig){ enable=pad_sig_tab[__i].enable;disable=pad_sig_tab[__i].disable"
	print "#define case_sig_equal(enable,disable) if(pad_sig_tab[__i].pad!=__pad&&pad_sig_tab[__i].sig==__sig){enable=pad_sig_tab[__i].enable;disable=pad_sig_tab[__i].disable"
	print "#define case_both_equal(enable,disable) if(pad_sig_tab[__i].pad==__pad&&pad_sig_tab[__i].sig==__sig){enable=pad_sig_tab[__i].enable;disable=pad_sig_tab[__i].disable"
	print "#define case_end };"
	print "#define foreach_pad_sig_end };}"
	print "static struct pad_sig pad_sig_tab[]={"
	for( combine in pads_pinmux )
	{
		split(combine,dim,SUBSEP)
		pad=dim[1];sig=dim[2];enable=dim[3];
		if(enable == "disable")
		{
			if(length ( pads_pinmux[pad,sig,"enable"] )!=0)
				continue;
			enable=NOT_EXIST;
			disable=pads_pinmux[pad,sig,"disable"];
		}else{
			enable=pads_pinmux[pad,sig,"enable"];
			disable="NOT_EXIST";
			if(length ( pads_pinmux[pad,sig,"disable"] )!=0)
			{ 
				disable=pads_pinmux[pad,sig,"disable"];
			}
		}
		printf "\t"
		print "{.pad=PAD_" pad ",.sig=SIG_" sig ",.enable="  enable \
			",.disable=" disable "},"
	}
	print "};"
	print "static const char * pad_name[]={"
	x=0;
	for(i in pads)
	{
		printf "\t[%d]=\"%s\",\n",pads[i],i;
		if(i in sigs){
			print "pad name " i " conflict" | "cat 1>&2" ;
			exit 1;
		}
		x++;
	}
	print "\t[PAD_MAX_PADS]=NULL\n};"
	
	print "static const char * sig_name[]={"
	x=0;
	for(i in sigs)
	{
		
		printf "\t[%d]=\"%s\",\n",sigs[i],i;
		if(i in pads){
			print "sig name " i " conflict" | "cat 1>&2" ;
			exit 1;
		}
		x++;
	}
	print "\t[SIG_GPIOIN]=\"GPIOIN\","
    print "\t[SIG_GPIOOUT]=\"GPIOOUT\","
	print "\t[SIG_MAX_SIGS]=NULL\n};"
	
	print "/* GPIO operation part */"
	x=0;
	print "static unsigned pad_gpio_bit[]={"
	for( combine in pads_gpio )
	{
		split(combine,dim,SUBSEP)
		pad=dim[1];sig=dim[2];
		if(sig != column[3])
		{
			continue;
		};
		if(x!=0)
			printf ",\n";
		printf "\t[PAD_%s]=%s",pad,pads_gpio[pad,sig];
		x++;
	}
	
	print "\n};"
	exit ret
}

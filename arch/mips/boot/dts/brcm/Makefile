dtb-$(CONFIG_DT_BCM93384WVG)		+= bcm93384wvg.dtb
dtb-$(CONFIG_DT_BCM93384WVG_VIPER)	+= bcm93384wvg_viper.dtb
dtb-$(CONFIG_DT_BCM96368MVWG)		+= bcm96368mvwg.dtb
dtb-$(CONFIG_DT_BCM9EJTAGPRB)		+= bcm9ejtagprb.dtb
dtb-$(CONFIG_DT_BCM97125CBMB)		+= bcm97125cbmb.dtb
dtb-$(CONFIG_DT_BCM97346DBSMB)		+= bcm97346dbsmb.dtb
dtb-$(CONFIG_DT_BCM97358SVMB)		+= bcm97358svmb.dtb
dtb-$(CONFIG_DT_BCM97360SVMB)		+= bcm97360svmb.dtb
dtb-$(CONFIG_DT_BCM97362SVMB)		+= bcm97362svmb.dtb
dtb-$(CONFIG_DT_BCM97420C)		+= bcm97420c.dtb
dtb-$(CONFIG_DT_BCM97425SVMB)		+= bcm97425svmb.dtb

obj-y				+= $(patsubst %.dtb, %.dtb.o, $(dtb-y))

# Force kbuild to make empty built-in.o if necessary
obj-				+= dummy.o

always				:= $(dtb-y)
clean-files			:= *.dtb *.dtb.S

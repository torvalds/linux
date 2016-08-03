dtb-$(CONFIG_DT_BCM93384WVG)		+= bcm93384wvg.dtb
dtb-$(CONFIG_DT_BCM93384WVG_VIPER)	+= bcm93384wvg_viper.dtb
dtb-$(CONFIG_DT_BCM96358NB4SER)		+= bcm96358nb4ser.dtb
dtb-$(CONFIG_DT_BCM96368MVWG)		+= bcm96368mvwg.dtb
dtb-$(CONFIG_DT_BCM9EJTAGPRB)		+= bcm9ejtagprb.dtb
dtb-$(CONFIG_DT_BCM97125CBMB)		+= bcm97125cbmb.dtb
dtb-$(CONFIG_DT_BCM97346DBSMB)		+= bcm97346dbsmb.dtb
dtb-$(CONFIG_DT_BCM97358SVMB)		+= bcm97358svmb.dtb
dtb-$(CONFIG_DT_BCM97360SVMB)		+= bcm97360svmb.dtb
dtb-$(CONFIG_DT_BCM97362SVMB)		+= bcm97362svmb.dtb
dtb-$(CONFIG_DT_BCM97420C)		+= bcm97420c.dtb
dtb-$(CONFIG_DT_BCM97425SVMB)		+= bcm97425svmb.dtb
dtb-$(CONFIG_DT_BCM97435SVMB)		+= bcm97435svmb.dtb

dtb-$(CONFIG_DT_NONE) += \
	bcm93384wvg.dtb \
	bcm93384wvg_viper.dtb \
	bcm96358nb4ser.dtb \
	bcm96368mvwg.dtb \
	bcm9ejtagprb.dtb \
	bcm97125cbmb.dtb \
	bcm97346dbsmb.dtb \
	bcm97358svmb.dtb \
	bcm97360svmb.dtb \
	bcm97362svmb.dtb \
	bcm97420c.dtb \
	bcm97425svmb.dtb \
	bcm97435svmb.dtb

obj-y				+= $(patsubst %.dtb, %.dtb.o, $(dtb-y))

# Force kbuild to make empty built-in.o if necessary
obj-				+= dummy.o

always				:= $(dtb-y)
clean-files			:= *.dtb *.dtb.S

dts-dirs	+= brcm
dts-dirs	+= cavium-octeon
dts-dirs	+= ingenic
dts-dirs	+= lantiq
dts-dirs	+= mti
dts-dirs	+= netlogic
dts-dirs	+= qca
dts-dirs	+= ralink
dts-dirs	+= xilfpga

obj-y		:= $(addsuffix /, $(dts-dirs))

always		:= $(dtb-y)
subdir-y	:= $(dts-dirs)
clean-files	:= *.dtb *.dtb.S

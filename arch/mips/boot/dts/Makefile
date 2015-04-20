dts-dirs	+= brcm
dts-dirs	+= cavium-octeon
dts-dirs	+= lantiq
dts-dirs	+= mti
dts-dirs	+= netlogic
dts-dirs	+= ralink

obj-y		:= $(addsuffix /, $(dts-dirs))

always		:= $(dtb-y)
subdir-y	:= $(dts-dirs)
clean-files	:= *.dtb *.dtb.S

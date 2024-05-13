#!/usr/bin/bash

if [ $1 = "gen-ramdisk" ]; then
	sudo dracut -v -f ramdisk.img $(uname -r) 
	echo "add-auto-load-safe-path ~/repositories/linux/scripts/gdb/vmlinux-gdb.py" >> ~/.gdbinit
fi

if [ $1 = "debug-gdb" ]; then
	sudo qemu-system-x86_64 -kernel vmlinux -initrd ramdisk.img -append "console=tty0 nokaslr" -enable-kvm -cpu host -m 512 -s -S
fi

if [ $1 = "exec" ]; then
	sudo qemu-system-x86_64 -kernel arch/x86_64/boot/bzImage -initrd ramdisk.img -enable-kvm -cpu host -m 512
fi

if [ $1 = 'help' ]; then
	echo " --- DEBUG POR: SIMOES, GUILHERME ---- "
	echo ""
	echo "+-------------------------------------------------------------+"
	echo "| *********** PARA FINS DE DEBUG COM QEMU ******************  |"
	echo "|execute: ./debug.sh debug-gdb                                |"
	echo "|depois em outro terminal exeute: gdb vmlinux                 |"
	echo "|depois dentro do prompt do gdb digite: target remove :1234   |"
	echo "+-------------------------------------------------------------+"
	echo ""
	echo ""
	echo "+---------------------------------------+"
	echo "| ************ EXECUTANDO ************* |"
	echo "| execute o comando: ./debug exec       |"
	echo "+---------------------------------------+"
fi




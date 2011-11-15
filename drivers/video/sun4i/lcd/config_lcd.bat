@echo off
	goto START

:START
    @echo *********************************************
    @echo *   select board                            *
    @echo *********************************************
    @echo  0: hv_800x480
    @echo  1: lvds_1080
    @echo  2: TC101+tl080wx800-v0
    @echo *********************************************

    set /p SEL=Please Select:
    if %SEL%==0     goto LCD0
    if %SEL%==1     goto LCD1
    if %SEL%==2     goto LCD2
    goto ERROR

::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
:LCD0
	copy lcd_bak\hv_800x480.c lcd0_panel_cfg.c
    goto conti
::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
:LCD1
	copy lcd_bak\lvds_1080.c lcd0_panel_cfg.c
    goto conti
::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
:LCD2
	copy lcd_bak\tl080wx800.c lcd0_panel_cfg.c
    goto conti
::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

:ERROR
	@echo error selection
	goto conti

:conti
		del lcd0_panel_cfg.o
    @echo *********************************************
    pause


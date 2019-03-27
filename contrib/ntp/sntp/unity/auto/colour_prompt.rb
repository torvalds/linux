# ==========================================
#   Unity Project - A Test Framework for C
#   Copyright (c) 2007 Mike Karlesky, Mark VanderVoord, Greg Williams
#   [Released under MIT License. Please refer to license.txt for details]
# ========================================== 

if RUBY_PLATFORM =~/(win|w)32$/
	begin
		require 'Win32API'
	rescue LoadError
		puts "ERROR! \"Win32API\" library not found"
		puts "\"Win32API\" is required for colour on a windows machine"
		puts "  try => \"gem install Win32API\" on the command line"
		puts
	end
	# puts
  # puts 'Windows Environment Detected...'
	# puts 'Win32API Library Found.'
	# puts
end

class ColourCommandLine
  def initialize
    if RUBY_PLATFORM =~/(win|w)32$/  
      get_std_handle = Win32API.new("kernel32", "GetStdHandle", ['L'], 'L')
      @set_console_txt_attrb =
      Win32API.new("kernel32","SetConsoleTextAttribute",['L','N'], 'I')
      @hout = get_std_handle.call(-11)
    end
  end
  
  def change_to(new_colour)
    if RUBY_PLATFORM =~/(win|w)32$/
      @set_console_txt_attrb.call(@hout,self.win32_colour(new_colour))
    else
	  	"\033[30;#{posix_colour(new_colour)};22m"
	 	end
  end
  
  def win32_colour(colour)
    case colour
      when :black then 0  
      when :dark_blue then 1
      when :dark_green then 2
      when :dark_cyan then 3
      when :dark_red then 4
      when :dark_purple then 5
      when :dark_yellow, :narrative then 6
      when :default_white, :default, :dark_white then 7
      when :silver then 8
      when :blue then 9
      when :green, :success then 10
      when :cyan, :output then 11
      when :red, :failure then 12
      when :purple then 13
      when :yellow then 14
      when :white then 15
      else
        0
    end
  end
	
	def posix_colour(colour)
	  case colour
      when :black then 30  
      when :red, :failure then 31
      when :green, :success then 32
			when :yellow then 33
      when :blue, :narrative then 34
      when :purple, :magenta then 35
      when :cyan, :output then 36
      when :white, :default_white, :default then 37
      else
        30
    end
  end
	
  def out_c(mode, colour, str)
    case RUBY_PLATFORM
			when /(win|w)32$/
			  change_to(colour)
				 $stdout.puts str if mode == :puts
				 $stdout.print str if mode == :print
			  change_to(:default_white)
			else
				$stdout.puts("#{change_to(colour)}#{str}\033[0m") if mode == :puts
				$stdout.print("#{change_to(colour)}#{str}\033[0m") if mode == :print
		end			
  end
end # ColourCommandLine

def colour_puts(role,str)  ColourCommandLine.new.out_c(:puts, role, str)  end
def colour_print(role,str) ColourCommandLine.new.out_c(:print, role, str) end


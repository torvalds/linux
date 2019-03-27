options(digits=4)
file2 <- "etf_summary"
etf <- scan(file1, list(day=0, sec=0, offset=0, stab=0))
r <- lsfit(etf$sec, etf$offset)
count<-length(etf$sec)
mean<-r$coef[[1]]
std<-sqrt(var(r$residuals))
slope<-r$coef[[2]] * 1000 
cat("\n", file=file2 , append=TRUE, fill=FALSE, sep="")
cat(file1, "\n", file=file2, append=TRUE, fill=FALSE, sep="")
cat("etf1 ",  count, ", T ", mean, " ns, R ", slope, " ps/s, std ", std, " us\n", file=file2, append=TRUE, fill=FALSE, sep="")
str <- paste("eps/", file1, ".eps", sep="")
postscript(str,  ,  ,  , 5, pointsize=18)
par(mgp=c(1, 0, 0), tck=0.03, mar=c(2, 2, 1, 1))
plot(etf$sec, etf$offset, type="l", xlab=paste("MJD", etf$day, "Time (s)"), ylab="External Offset (ns)", ylim=c(-400, 400))
